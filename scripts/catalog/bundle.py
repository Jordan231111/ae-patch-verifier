"""Read Android package identities and signatures without opening module code."""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from zipfile import ZipFile


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def sdk_tool(name):
    path = shutil.which(name)
    if path:
        return path
    root = Path(os.environ["ANDROID_HOME"]) / "build-tools"
    installed = sorted((p for p in root.iterdir() if re.fullmatch(r"\d+(\.\d+)*", p.name)),
                       key=lambda p: tuple(map(int, p.name.split("."))), reverse=True)
    return str(next(p / name for p in installed if (p / name).is_file()))


def badging(apk):
    result = subprocess.check_output([sdk_tool("aapt"), "dump", "badging", apk],
                                     text=True, errors="replace", stderr=subprocess.DEVNULL)
    line = next(line for line in result.splitlines() if line.startswith("package: "))
    fields = dict(re.findall(r"(\w+)='([^']*)'", line))
    minimum = re.search(r"^sdkVersion:'(\d+)'", result, re.M)
    return (fields["name"], fields["versionName"], int(fields["versionCode"]),
            fields.get("split", ""), int(minimum[1]) if minimum else 1)


def certificate(apk, minimum=None):
    options = ["--min-sdk-version", str(minimum)] if minimum else []
    result = subprocess.check_output([sdk_tool("apksigner"), "verify", *options, "--print-certs", apk],
                                     text=True, stderr=subprocess.DEVNULL)
    certs = set(re.findall(r"^(?:V\d+(?:\.\d+)? )?Signer[^\n]*? certificate SHA-256 digest: ([a-f0-9]{64})$",
                           result, re.M))
    if len(certs) != 1:
        raise ValueError("Expected one verified APK signer")
    return certs.pop()


def verify_bundle(path, package, expected_version=None, expected_cert=None, expected_code=None):
    with tempfile.TemporaryDirectory() as temp, ZipFile(path) as archive:
        names = [item.filename for item in archive.infolist() if item.filename.endswith(".apk")]
        if (not names or len(set(names)) != len(names)
                or any(not re.fullmatch(r"[a-zA-Z0-9._-]+\.apk", name) for name in names)):
            raise ValueError("Invalid APK set entries")
        entries = {}
        for name in names:
            apk = Path(archive.extract(name, temp))
            app, version, code, split, minimum = badging(apk)
            if app != package or code <= 0 or split in entries:
                raise ValueError("APK identity or split set does not match the catalog entry")
            entries[split] = (apk, version, code, minimum)
        if "" not in entries or "config.arm64_v8a" not in entries:
            raise ValueError("An ARM64 base and matching signed splits are required")
        base, version, code, minimum = entries[""]
        signer = certificate(base, minimum)
        if not version or (expected_cert and signer != expected_cert):
            raise ValueError("Missing app version or signing identity changed")
        for split, (apk, split_version, split_code, _) in entries.items():
            if split and (split_version not in ("", version) or split_code != code or certificate(apk, minimum) != signer):
                raise ValueError("Split does not match the signed base APK")
        if (expected_version and version != expected_version) or (expected_code and code != int(expected_code)):
            raise ValueError(f"Expected {expected_version} ({expected_code}), received {version} ({code})")
    return dict(version=version, versionCode=code, signer=signer, sha256=digest(path),
                sizeBytes=path.stat().st_size)
