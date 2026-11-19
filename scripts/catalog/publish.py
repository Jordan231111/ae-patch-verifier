"""Publish verified, immutable APKS assets and atomically advance release metadata."""

import base64
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


BRANCH = "workshop-ready"
REPO = os.environ.get("GITHUB_REPOSITORY", "Jordan231111/ae-patch-verifier")


def gh(*args, **kwargs):
    return subprocess.run(["gh", *args], check=True, text=True, capture_output=True, **kwargs).stdout


def api(path, method="GET", data=None):
    args = ["api", f"repos/{REPO}/{path}", "--method", method]
    try:
        return json.loads(gh(*args, *(["--input", "-"] if data is not None else []),
                             input=json.dumps(data) if data is not None else None))
    except subprocess.CalledProcessError as error:
        if method == "GET" and "(HTTP 404)" in error.stderr:
            return None
        raise


def publish_asset(key, path, receipt):
    fingerprint = receipt["sha256"]
    if not re.fullmatch(r"[a-z0-9-]+", key) or not re.fullmatch(r"[\w.-]+", receipt["version"], re.ASCII):
        raise ValueError("Invalid variant or app version")
    tag = f"catalog-{key}-{fingerprint[:20]}"
    filename = f'{key}-{receipt["version"]}.apks'
    release = api(f"releases/tags/{tag}")
    if not release:
        # Unpublished drafts can be found by gh release view, including on retries.
        exists = subprocess.run(["gh", "release", "view", tag, "-R", REPO],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not exists:
            gh("release", "create", tag, "-R", REPO, "--draft", "--latest=false",
               "--title", f'{key} {receipt["version"]}', "--notes", f"SHA-256: {fingerprint}")
        info = json.loads(gh("release", "view", tag, "-R", REPO, "--json", "databaseId"))
        release = api(f'releases/{info["databaseId"]}')
    asset = next((a for a in release["assets"] if a["name"] == filename), None)
    if not asset and release["draft"]:
        with tempfile.TemporaryDirectory(dir=path.parent) as folder:
            upload = Path(folder) / filename
            os.link(path, upload)
            gh("release", "upload", tag, str(upload), "-R", REPO)
        assets = api(f'releases/{release["id"]}/assets')
        asset = next((a for a in assets if a["name"] == filename), None)
    if not asset or (asset["size"], asset["digest"], asset["state"]) != (
            receipt["sizeBytes"], f"sha256:{fingerprint}", "uploaded"):
        raise ValueError("Published asset does not match the verified local bundle")
    if release["draft"]:
        gh("release", "edit", tag, "-R", REPO, "--draft=false", "--latest=false")
    # Draft asset links may contain GitHub's temporary "untagged-..." placeholder.
    published = api(f"releases/tags/{tag}")
    final = next((a for a in published["assets"] if a["id"] == asset["id"]), None)
    expected_url = f"https://github.com/{REPO}/releases/download/{tag}/{filename}"
    if published["draft"] or not final or final["browser_download_url"] != expected_url:
        raise ValueError("The release does not have its permanent download address yet")
    return {**receipt, "downloadUrl": expected_url,
            "updatedAt": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")}


def ready_file():
    file = api(f"contents/ready.json?ref={BRANCH}")
    return (json.loads(base64.b64decode(file["content"])), file["sha"]) if file else ({}, None)


def update_ready(key, record, previous_digest):
    for attempt in range(9):
        entries, revision = ready_file()
        current = entries.get(key, {})
        if all(current.get(field) == record.get(field) for field in ("sha256", "requestKey", "downloadUrl")):
            return
        if current and current["sha256"] != previous_digest:
            raise ValueError("Another publisher already advanced this variant")
        entries[key] = record
        payload = {
            "branch": BRANCH, "message": f'Publish {key} {record["version"]}',
            "content": base64.b64encode((json.dumps(entries, indent=2, sort_keys=True) + "\n").encode()).decode()
        }
        if revision:
            payload["sha"] = revision
        try:
            api("contents/ready.json", "PUT", payload)
            return
        except subprocess.CalledProcessError as error:
            # Parallel variants update the same small file using GitHub's compare-and-swap.
            if "(HTTP 409)" not in error.stderr and "(HTTP 422)" not in error.stderr:
                raise
            if attempt == 8:
                raise
            time.sleep(1 + attempt)
