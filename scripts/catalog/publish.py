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
        result = gh(*args, *(["--input", "-"] if data is not None else []),
                    input=json.dumps(data) if data is not None else None)
        return json.loads(result) if result.strip() else None
    except subprocess.CalledProcessError as error:
        if method == "GET" and "(HTTP 404)" in error.stderr:
            return None
        raise


def stage_asset(key, version, path, fingerprint):
    if not re.fullmatch(r"[a-z0-9-]+", key) or not re.fullmatch(r"[\w.-]+", version, re.ASCII):
        raise ValueError("Invalid variant or app version")
    tag = f"catalog-{key}-{fingerprint[:20]}"
    filename = f"{key}-{version}.apks"
    release = api(f"releases/tags/{tag}")
    if not release:
        # A draft interrupted before publication may only be discoverable with the CLI.
        previous = subprocess.run(["gh", "release", "view", tag, "-R", REPO, "--json", "databaseId"],
                                  capture_output=True, text=True)
        if previous.returncode == 0:
            release = api(f'releases/{json.loads(previous.stdout)["databaseId"]}')
    if not release:
        try:
            release = api("releases", "POST", {"tag_name": tag, "draft": True, "make_latest": "false",
                                               "name": f"{key} {version}", "body": f"SHA-256: {fingerprint}"})
        except subprocess.CalledProcessError as error:
            if "(HTTP 422)" not in error.stderr:
                raise
            info = json.loads(gh("release", "view", tag, "-R", REPO, "--json", "databaseId"))
            release = api(f'releases/{info["databaseId"]}')
    asset = next((a for a in release["assets"] if a["name"] == filename), None)
    if asset and asset["state"] == "starter" and release["draft"]:
        # GitHub can leave an empty staging asset after an interrupted upload.
        api(f'releases/assets/{asset["id"]}', "DELETE")
        asset = None
    if not asset and release["draft"]:
        with tempfile.TemporaryDirectory(dir=path.parent) as folder:
            upload = Path(folder) / filename
            os.link(path, upload)
            gh("release", "upload", tag, str(upload), "-R", REPO)
        assets = api(f'releases/{release["id"]}/assets')
        asset = next((a for a in assets if a["name"] == filename), None)
    if not asset or (asset["size"], asset["digest"], asset["state"]) != (
            path.stat().st_size, f"sha256:{fingerprint}", "uploaded"):
        raise ValueError("Stored asset does not match the local bundle")
    return release, asset


def publish_verified(release, asset, receipt):
    if asset["digest"] != "sha256:" + receipt["sha256"] or asset["size"] != receipt["sizeBytes"]:
        raise ValueError("Verification and storage receipts disagree")
    if release["draft"]:
        release = api(f'releases/{release["id"]}', "PATCH", {"draft": False, "make_latest": "false"})
    # Draft asset links can contain a temporary untagged placeholder.
    url = f'https://github.com/{REPO}/releases/download/{release["tag_name"]}/{asset["name"]}'
    if not any(a["id"] == asset["id"] and a["browser_download_url"] == url for a in release["assets"]):
        release = api(f'releases/tags/{release["tag_name"]}')
    if release["draft"] or not any(a["id"] == asset["id"] and a["browser_download_url"] == url for a in release["assets"]):
        raise ValueError("The verified release does not have its permanent download URL")
    return {**receipt, "downloadUrl": url,
            "updatedAt": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")}


def ready_file():
    file = api(f"contents/ready.json?ref={BRANCH}")
    if file and file.get("encoding") == "none":
        raw = gh("api", f"repos/{REPO}/contents/ready.json?ref={BRANCH}", "-H", "Accept: application/vnd.github.raw+json")
        return json.loads(raw), file["sha"]
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
