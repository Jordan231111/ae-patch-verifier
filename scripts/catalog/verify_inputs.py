"""Check the complete publisher APK set and selected module before re-signing."""
import argparse
import json
from pathlib import Path
import re
import zipfile

from bundle import badging, certificate


def verify_source(folder, expected_package, expected_code, expected_cert):
    manifest = json.loads((folder / "manifest.json").read_bytes())
    if manifest.get("expansions") or any(folder.rglob("*.obb")):
        raise ValueError("External expansion files are unsupported")
    names = [part["file"] for part in manifest["split_apks"]]
    if not names or len(set(names)) != len(names):
        raise ValueError("Empty or duplicate source APK set")
    identities = {}
    for name in names:
        if not re.fullmatch(r"[A-Za-z0-9._-]+\.apk", name):
            raise ValueError("Invalid source APK path")
        apk = folder / name
        package, version, code, split, minimum = badging(apk)
        if package != expected_package or code != int(expected_code) or split in identities:
            raise ValueError("Source package, version or split identity differs")
        if certificate(apk, minimum) != expected_cert:
            raise ValueError("Source APK signer differs")
        with zipfile.ZipFile(apk) as archive:
            entries = archive.namelist()
            if len(entries) != len(set(entries)) or "assets/lspatch/config.json" in entries:
                raise ValueError("Expected an unmodified publisher APK")
        identities[split] = version
    if "" not in identities or "config.arm64_v8a" not in identities:
        raise ValueError("The source needs one base and its ARM64 split")
    if any(v and v != identities[""] for v in identities.values()):
        raise ValueError("Source split version names differ")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--code", type=int, required=True)
    parser.add_argument("--source-cert", required=True)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--module-cert", required=True)
    args = parser.parse_args()
    verify_source(args.source, args.package, args.code, args.source_cert.lower().replace(":", ""))
    if certificate(args.module) != args.module_cert.lower().replace(":", ""):
        raise ValueError("Module does not use the selected shared signing identity")
    with zipfile.ZipFile(args.module) as module:
        if args.package not in module.read("META-INF/xposed/scope.list").decode().splitlines():
            raise ValueError("Module does not support this target")
    print("Verified all publisher APK identities, splits and signatures, and the shared-signed module")


if __name__ == "__main__":
    main()
