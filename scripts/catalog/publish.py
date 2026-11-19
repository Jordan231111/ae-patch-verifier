"""Publish verified, immutable APKS assets and atomically advance release metadata."""

import argparse
import base64
from datetime import datetime, timedelta, timezone
import json
import os
import random
from pathlib import Path
import re
import subprocess
import time


BRANCH = "workshop-ready"
REPO = os.environ.get("GITHUB_REPOSITORY", "Jordan231111/ae-patch-verifier")


def delivery_dir():
    return Path(os.environ["RUNNER_TEMP"]) / "workshop-delivery"


def bundle_name(key, version):
    if not re.fullmatch(r"[a-z0-9-]+", key) or not re.fullmatch(r"[\w.-]+", version, re.ASCII):
        raise ValueError("Invalid variant or app version")
    return f"{key}-{version}.apks"


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
    tag = f"catalog-{key}-{fingerprint[:20]}"
    filename = bundle_name(key, version)
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
        if path.name != filename:
            raise ValueError("Staged bundle has the wrong filename")
        gh("release", "upload", tag, str(path), "-R", REPO)
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


def read_record(path):
    endpoint = f"repos/{REPO}/contents/{path}?ref={BRANCH}"
    file = api(f"contents/{path}?ref={BRANCH}")
    if file and file.get("encoding") == "none":
        raw = gh("api", endpoint, "-H", "Accept: application/vnd.github.raw+json")
        return json.loads(raw), file["sha"]
    return (json.loads(base64.b64decode(file["content"])), file["sha"]) if file else ({}, None)


def ready_file():
    return read_record("ready.json")


def announce(variant, record):
    run_id = int(os.environ["GITHUB_RUN_ID"])
    asset_id = int(os.environ["WORKSHOP_ARTIFACT_ID"])
    name = bundle_name(variant, record["version"])
    if asset_id <= 0 or os.environ["WORKSHOP_ARTIFACT_DIGEST"].removeprefix("sha256:") != record["sha256"]:
        raise ValueError("Uploader and verified bundle receipts disagree")
    path = f"requests/{variant}.json"
    for attempt in range(8):
        ticket, revision = read_record(path)
        if (ticket.get("key") != record["requestKey"] or ticket.get("nonce") != os.environ["WORKSHOP_NONCE"]
                or ticket.get("runId", run_id) != run_id or ticket.get("version") != record["version"]):
            print("The original request is no longer current.")
            return
        # GitHub's REST read replica can lag the successful upload. Readers confirm
        # the stored object and its actual expiry before handing out a signed URL.
        now = datetime.now(timezone.utc)
        ticket.update(runId=run_id, transfer={**record, "variant": variant, "artifactId": asset_id,
                      "runId": run_id, "expiresAt": (now + timedelta(hours=23)).isoformat(),
                      "updatedAt": now.isoformat(timespec="seconds")})
        try:
            api("contents/" + path, "PUT", {"sha": revision, "branch": BRANCH, "message": f"Offer verified {name}",
                                            "content": base64.b64encode(json.dumps(ticket).encode()).decode()})
            print(f"Verified download available for {variant}.", flush=True)
            return
        except subprocess.CalledProcessError as error:
            if attempt == 7 or not any(f"(HTTP {n})" in error.stderr for n in (409, 422)):
                raise
            time.sleep(random.uniform(0.05, min(1.0, 0.15 * 2 ** attempt)))


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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=["announce", "archive"])
    args = parser.parse_args()
    payload = json.loads((delivery_dir() / "receipt.json").read_bytes())
    key, record = payload["variant"], payload["record"]
    if args.operation == "announce":
        announce(key, record)
    else:
        path = delivery_dir() / bundle_name(key, record["version"])
        release, asset = stage_asset(key, record["version"], path, record["sha256"])
        published = publish_verified(release, asset, record)
        update_ready(key, published, payload["previous"])
