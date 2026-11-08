#!/usr/bin/env python3
"""Build AllRelic's complete ARM64 split set with a verified API-102 prebuilt."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

PACKAGE = "com.AllRelic"
MODULE = "com.arkit.core"
PATCHER_SHA256 = "d238fdc414d121b7fa454d8b4ccf420df3a8c97d563761861ff92bd9c5da2165"
HOST_CERT = "c081890cf2a1adf13e56d7b50a4f3d8edb35b7c46d6ccc732dd997b7e433be1d"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(*args):
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{Path(str(args[0])).name} failed: {result.stdout}\n{result.stderr}")
    return result.stdout


def certificate(apksigner, apk, expected):
    require(re.fullmatch(r"[0-9a-f]{64}", expected) is not None, "Invalid certificate pin")
    output = run(apksigner, "verify", "--print-certs", apk)
    digests = {line.rsplit(": ", 1)[-1].lower() for line in output.splitlines()
               if "certificate SHA-256 digest:" in line and not line.startswith("Source Stamp")}
    require(digests == {expected}, f"Unexpected APK signing identity: {Path(apk).name}")


def badging(aapt, apk):
    output = run(aapt, "dump", "badging", apk)
    line = next((line for line in output.splitlines() if line.startswith("package: ")), "")
    values = dict(re.findall(r"(\w+)='([^']*)'", line))
    require(values.get("name"), f"Missing Android package identity: {apk}")
    return values


def source_splits(manifest, source, version_code, version_name):
    require(manifest.get("package_name") == PACKAGE, "Wrong XAPK package")
    require(str(manifest.get("version_code")) == str(version_code), "Wrong XAPK versionCode")
    require(manifest.get("version_name") == version_name, "Wrong XAPK version name")
    splits = manifest.get("split_apks")
    require(isinstance(splits, list) and len(splits) > 1, "Missing split set")
    ids, files = set(), set()
    for split in splits:
        require(isinstance(split, dict), "Invalid split descriptor")
        name, split_id = split.get("file", ""), split.get("id", "")
        require(isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9._-]+\.apk", name), "Unsafe split path")
        require(isinstance(split_id, str) and split_id and split_id not in ids and name not in files,
                "Missing or duplicate split identity")
        require((source / name).is_file(), f"Missing split: {name}")
        ids.add(split_id)
        files.add(name)
    require("base" in ids and "config.arm64_v8a" in ids, "Missing base or ARM64 split")
    require(not ids.intersection({"config.armeabi_v7a", "config.x86", "config.x86_64"}), "Mixed architecture split set")
    require(files == {p.name for p in source.glob("*.apk")}, "Unlisted APK in source split set")
    return splits


def verify_module(apk):
    with zipfile.ZipFile(apk) as archive:
        entries = archive.namelist()
        require(len(entries) == len(set(entries)), "Duplicate module ZIP entries")
        require("assets/xposed_init" not in entries, "Classic Xposed module is unsupported")
        props = dict(line.split("=", 1) for line in archive.read("META-INF/xposed/module.prop").decode().splitlines()
                     if "=" in line and not line.startswith("#"))
        require(props.get("id") == MODULE and props.get("minApiVersion") == "102"
                and props.get("targetApiVersion") == "102", "Expected ARKit API 102 module")
        require(archive.read("META-INF/xposed/java_init.list").decode().strip() == MODULE + ".ModuleEntry",
                "Wrong ARKit entry point")
        require(archive.read("META-INF/xposed/scope.list").decode().strip() == PACKAGE, "Wrong ARKit scope")
        native = [entry for entry in entries if entry.startswith("lib/") and entry.endswith(".so")]
        require(native and all(entry.startswith("lib/arm64-v8a/") for entry in native), "Wrong module ABI")
        for entry in entries:
            if re.fullmatch(r"classes\d*\.dex", entry):
                require(b"de/robv/android/xposed" not in archive.read(entry), "Classic Xposed API in module")


def verify_embedded(base, source_base, module):
    with zipfile.ZipFile(base) as archive:
        require(archive.testzip() is None, "Corrupt patched base")
        config = json.loads(archive.read("assets/lspatch/config.json"))
        require(config.get("useManager") is False and config.get("sigBypassLevel") == 2,
                "Wrong integrated LSPatch configuration")
        lsp = config.get("lspConfig", {})
        require(lsp.get("API_CODE") == 102 and lsp.get("VERSION_CODE") == 487
                and lsp.get("VERSION_NAME") == "1.2", "Wrong LSPatch runtime")
        modules = [name for name in archive.namelist() if name.startswith("assets/lspatch/modules/")]
        require(modules == [f"assets/lspatch/modules/{MODULE}.apk"], "Wrong embedded module set")
        for entry, path in [(modules[0], module), ("assets/lspatch/origin.apk", source_base)]:
            with archive.open(entry) as stream:
                require(hashlib.file_digest(stream, "sha256").hexdigest() == sha256(path),
                        f"Embedded input changed: {entry}")


def build(args):
    source, module, patcher, output = [Path(value).resolve() for value in
                                        [args.source, args.module, args.patcher, args.output]]
    require(re.fullmatch(r"[0-9a-f]{40}", args.module_sha), "Invalid module commit")
    require(re.fullmatch(r"[1-9][0-9]*", args.version_code), "Invalid versionCode")
    require(not output.exists(), "Output already exists; refusing to replace a published bundle")
    require(sha256(patcher) == PATCHER_SHA256, "LSPatch JAR integrity check failed")
    sdk = Path(args.sdk).resolve()
    apksigner, zipalign, aapt = [sdk / tool for tool in ["apksigner", "zipalign", "aapt2"]]
    manifest = json.loads((source / "manifest.json").read_text())
    splits = source_splits(manifest, source, args.version_code, args.version_name)
    base_name = next(split["file"] for split in splits if split["id"] == "base")
    has_unity = False
    for split in splits:
        apk = source / split["file"]
        certificate(apksigner, apk, args.source_cert)
        package = badging(aapt, apk)
        require(package.get("name") == PACKAGE and package.get("versionCode") == args.version_code,
                "Split Android package/version does not match XAPK manifest")
        if split["id"] == "base":
            require(not package.get("split") and package.get("versionName") == args.version_name, "Wrong base identity")
        else:
            require(package.get("split") == split["id"], "Wrong Android split identity")
        with zipfile.ZipFile(apk) as archive:
            require(archive.testzip() is None, "Corrupt source APK")
            native = [name for name in archive.namelist() if name.startswith("lib/") and name.endswith(".so")]
            require(all(name.startswith("lib/arm64-v8a/") for name in native), "Unexpected native source ABI")
            has_unity |= "lib/arm64-v8a/libil2cpp.so" in native
    require(has_unity, "AllRelic Unity ARM64 library is missing")
    certificate(apksigner, module, args.module_cert)
    require(badging(aapt, module).get("name") == MODULE, "Wrong Android module package")
    verify_module(module)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="allrelic-package-", dir=output.parent) as temp:
        work = Path(temp)
        key = work / "host.jks"
        with zipfile.ZipFile(patcher) as archive:
            key.write_bytes(archive.read("assets/keystore"))
        run("java", "-jar", patcher, source / base_name, "-m", module, "-o", work / "patched",
            "-l", "2", "-k", key, "123456", "key0", "123456", "-f")
        candidates = list((work / "patched").glob("*-lspatched.apk"))
        require(len(candidates) == 1, "Patcher did not produce exactly one base APK")
        bundle = work / "bundle"
        bundle.mkdir()
        normalized = {**manifest, "split_apks": [], "split_configs": []}
        normalized.pop("icon", None)
        for split in splits:
            name = "base.apk" if split["id"] == "base" else split["file"]
            original = candidates[0] if split["id"] == "base" else source / split["file"]
            aligned = work / (name + ".aligned")
            run(zipalign, "-f", "-P", "16", "4", original, aligned)
            run(apksigner, "sign", "--v4-signing-enabled", "false", "--alignment-preserved", "true",
                "--ks", key, "--ks-pass", "pass:123456", "--ks-key-alias", "key0", "--key-pass", "pass:123456",
                "--out", bundle / name, aligned)
            certificate(apksigner, bundle / name, HOST_CERT)
            identity = badging(aapt, bundle / name)
            require(identity.get("name") == PACKAGE and identity.get("versionCode") == args.version_code,
                    "Patched Android package identity changed")
            # The pinned multi-ABI loader can leave unused architectures unaligned; ARM64
            # and all other package entries must pass the Android alignment check.
            alignment = subprocess.run([str(zipalign), "-c", "-P", "16", "-v", "4", str(bundle / name)],
                                       text=True, capture_output=True)
            bad = [line for line in alignment.stdout.splitlines() if "(BAD - " in line]
            require(all(re.search(r"assets/lspatch/so/(armeabi-v7a|x86|x86_64)/liblspatch\.so \(BAD - ", line)
                        for line in bad), "Patched APK alignment failed")
            require(alignment.returncode == 0 or (alignment.returncode == 1 and bad), "zipalign verification failed")
            normalized["split_apks"].append({"file": name, "id": split["id"]})
            if split["id"] != "base":
                normalized["split_configs"].append(split["id"])
        verify_embedded(bundle / "base.apk", source / base_name, module)
        normalized["total_size"] = sum(p.stat().st_size for p in bundle.glob("*.apk"))
        (bundle / "manifest.json").write_text(json.dumps(normalized, ensure_ascii=False) + "\n")
        info = {"schema": 1, "packageName": PACKAGE, "architecture": "arm64-v8a", "source": "apkpure",
                "versionCode": args.version_code, "versionName": args.version_name,
                "moduleSha": args.module_sha, "moduleSha256": sha256(module), "moduleCertSha256": args.module_cert,
                "sourceCertSha256": args.source_cert, "hostCertSha256": HOST_CERT,
                "lspatchSha256": PATCHER_SHA256, "api": 102, "runtimeValidated": False,
                "apks": {p.name: sha256(p) for p in sorted(bundle.glob("*.apk"))}}
        (bundle / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
        pending = work / "download.apks"
        with zipfile.ZipFile(pending, "w", compression=zipfile.ZIP_STORED) as archive:
            for path in sorted(bundle.iterdir()):
                archive.write(path, path.name)
        with zipfile.ZipFile(pending) as archive:
            require(archive.testzip() is None, "Final split bundle integrity failed")
        shutil.move(pending, output)
    print(json.dumps({"output": str(output), "sizeBytes": output.stat().st_size, "sha256": sha256(output)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["source", "module", "patcher", "output", "sdk", "module-sha", "version-code", "version-name",
                 "source-cert", "module-cert"]:
        parser.add_argument("--" + name, required=True)
    build(parser.parse_args())


if __name__ == "__main__":
    main()
