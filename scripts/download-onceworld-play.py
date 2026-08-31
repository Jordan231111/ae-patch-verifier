#!/usr/bin/env python3

import argparse
import base64
import gzip
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path

from gplaydl.api import get_delivery, get_details, purchase
from gplaydl.auth import ensure_auth


def play_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return base64.urlsafe_b64encode(digest.digest()).decode().rstrip("=")


def download_with_aria(files, output: Path) -> None:
    input_file = output / "aria2-input"
    lines = []
    for item in files:
        lines.extend([item["url"], f"  out={item['raw'].name}"])
        if item["cookies"]:
            cookie = "; ".join(f"{value['name']}={value['value']}" for value in item["cookies"])
            lines.append(f"  header=Cookie: {cookie}")
    input_file.write_text("\n".join(lines) + "\n")

    subprocess.run([
        "aria2c", "--input-file", str(input_file), "--dir", str(output),
        "--console-log-level=warn", "--summary-interval=0",
        "--max-concurrent-downloads=4", "--max-connection-per-server=16",
        "--split=16", "--min-split-size=2M", "--file-allocation=none",
        "--max-tries=3", "--retry-wait=2", "--connect-timeout=20", "--timeout=120",
        "--allow-overwrite=true", "--auto-file-renaming=false"
    ], check=True)
    input_file.unlink(missing_ok=True)

    for item in files:
        raw = item["raw"]
        final = item["final"]
        if item["gzipped"]:
            with gzip.open(raw, "rb") as source, final.open("wb") as destination:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    destination.write(chunk)
            raw.unlink()
        if play_digest(final) != item["sha256"].rstrip("="):
            raise RuntimeError(f"Google Play hash mismatch for {final.name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True)
    parser.add_argument("--architecture", required=True)
    parser.add_argument("--expected-version", default="unknown")
    parser.add_argument("--metadata-source", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    auth = ensure_auth(arch="arm64")
    if not auth:
        raise RuntimeError("Google Play authentication failed")
    app = get_details(args.package, auth)
    delivery = get_delivery(args.package, app.version_code, auth, purchase(args.package, app.version_code, auth))
    if delivery.additional_files:
        raise RuntimeError("Google Play returned OBB or asset files outside the APKS contract")
    if args.metadata_source == "google-play" and args.expected_version != "unknown" \
            and app.version_string != args.expected_version:
        raise RuntimeError(
            f"Google Play delivered {app.version_string} but its listing advertised {args.expected_version}"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    files = []

    def add_file(name, url, gzipped_url, sha256, cookies):
        if not re.fullmatch(r"[A-Za-z0-9._-]+\.apk", name) or not sha256:
            raise RuntimeError(f"Invalid Google Play file metadata for {name}")
        use_gzip = bool(gzipped_url)
        final = args.output / name
        files.append({
            "url": gzipped_url if use_gzip else url,
            "raw": args.output / f"{name}.gz" if use_gzip else final,
            "final": final,
            "gzipped": use_gzip,
            "sha256": sha256,
            "cookies": cookies,
        })

    add_file("base.apk", delivery.download_url, delivery.gzipped_url, delivery.sha256, delivery.cookies)
    split_entries = []
    for split in sorted(delivery.splits, key=lambda value: value.name):
        add_file(f"{split.name}.apk", split.url, split.gzipped_url, split.sha256, [])
        split_entries.append({"id": split.name, "file": f"{split.name}.apk"})

    expected_split = f"config.{args.architecture.replace('-', '_')}"
    if expected_split not in {entry["id"] for entry in split_entries}:
        raise RuntimeError(f"Google Play split set is missing {expected_split}")

    download_with_aria(files, args.output)
    manifest = {
        "package_name": args.package,
        "version_code": app.version_code,
        "version_name": app.version_string,
        "split_apks": [{"id": "base", "file": "base.apk"}, *split_entries],
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, separators=(",", ":")))
    (args.output / "source.env").write_text(
        f"VERSION_CODE={shlex.quote(str(app.version_code))}\n"
        f"VERSION_NAME={shlex.quote(app.version_string)}\n"
        "SOURCE_KIND=google-play\n"
    )
    print(f"[source] Google Play delivered {app.version_string} code={app.version_code} with {len(files)} parallel files")


if __name__ == "__main__":
    main()
