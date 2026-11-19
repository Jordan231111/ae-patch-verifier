"""Plan upstream changes, then prepare only changed ARM64 workshop releases."""

import argparse
import base64
from functools import cache
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlencode, urlsplit
from urllib.request import Request, urlopen

from bundle import certificate, digest, verify_bundle
from publish import publish_asset, ready_file, update_ready


ROOT = Path(__file__).resolve().parents[2]
SITE = "https://verify-ae-modmenu.vercel.app"
CATALOG = json.loads((ROOT / "catalog.json").read_bytes())["apps"]
INPUTS = json.loads((ROOT / "scripts/catalog/inputs.json").read_bytes())


def site_url(path):
    if not isinstance(path, str) or not path.startswith("/api/") or urlsplit(path).netloc:
        raise ValueError("Unexpected workshop API route")
    return SITE + path


def site_json(path, data=None):
    payload = json.dumps(data).encode() if data is not None else None
    request = Request(site_url(path), data=payload, headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=45) as response:
        return json.load(response)


def lookup(package, strategy):
    return json.loads(subprocess.check_output(
        ["node", ROOT / "scripts/catalog/lookup.js", package, strategy], timeout=85))


def play(package, folder, *options):
    # Each subprocess gets only the credentials it uses. Its diagnostic output stays on the runner.
    subprocess.run([sys.executable, ROOT / "scripts/download-play-apks.py", "--package", package,
                    "--architecture", "arm64-v8a", "--account-email", os.getenv("PLAY_EMAIL", ""),
                    "--metadata-source", "google-play", "--output", folder, *options],
                   check=True, capture_output=True, timeout=45 if "--metadata-only" in options else 330,
                   env=child_env("GPLAYDL_API_KEY"))


def child_env(*extra):
    names = {"PATH", "HOME", "LANG", "TMPDIR", "ANDROID_HOME", "ANDROID_SDK_ROOT", "JAVA_HOME", *extra}
    return {name: value for name, value in os.environ.items() if name in names}


@cache
def upstream(package, mirror_only):
    if mirror_only:
        return lookup(package, "apkpure")
    with tempfile.TemporaryDirectory() as folder:
        try:
            play(package, folder, "--metadata-only")
            result = json.loads((Path(folder) / "play-version.json").read_bytes())
            return dict(versionName=result["version_name"], versionCode=int(result["version_code"]), source="google-play")
        except (subprocess.SubprocessError, OSError, ValueError):
            # Public listing metadata can precede delivery to our Play device.
            listing = lookup(package, "auto")
            if not listing.get("versionCode"):
                try:
                    mirror = lookup(package, "apkpure")
                    if mirror["versionName"] == listing["versionName"]:
                        listing["versionCode"] = mirror["versionCode"]
                except subprocess.SubprocessError:
                    pass
            return listing


@cache
def module_ref(app, flavor):
    route = ("/api/module/latest?" + urlencode({"moduleSource": "main", "variant": flavor})
             if app == "anothereden" else f"/api/{app}/module/latest")
    data = site_json(route)
    sha = data.get("sha") or data.get("shortSha")
    if data.get("prebuilt") is False or not re.fullmatch(r"[0-9a-f]{7,40}", sha or ""):
        raise ValueError("A published module build is not available")
    return sha


def plan(selected, verify):
    records, _ = ready_file()
    todo, failures = [], []
    for app in CATALOG:
        key = app["id"]
        if app.get("statusUrl") or selected not in ("all", key):
            continue
        for variant in app["variants"]:
            ident = variant["id"]
            current = records.get(ident, variant)
            try:
                source = upstream(variant["packageName"], key == "allrelic" or variant.get("region") == "japan")
                version, code = source["versionName"], source.get("versionCode")
                if not version or (code is not None and int(code) <= 0):
                    raise ValueError("Upstream did not identify an app release")
                if code and int(code) < int(current["versionCode"]):
                    raise ValueError("Upstream version code is older than the published bundle")
                if key in INPUTS:
                    module = INPUTS[key]["moduleSha256"]
                    module_current = module == current.get("moduleDigest")
                else:
                    module = module_ref(key, variant.get("flavor", "release"))
                    saved = current.get("moduleSha") or ""
                    module_current = bool(saved) and (saved.startswith(module) or module.startswith(saved))
                changed = version != current["version"] or (code and int(code) != current["versionCode"]) or not module_current
                if changed or verify:
                    todo.append(dict(app=key, variant=ident, version=version, code=code, source=source["source"],
                                     module=module, previous=current["sha256"]))
                print(f"{ident}: {version} ({code or 'store listing'}) – {'prepare' if changed or verify else 'current'}")
            except Exception as error:
                failures.append(ident)
                print(f"::warning::{ident}: metadata check failed ({type(error).__name__})")
    if selected != "all" and not any(a["id"] == selected and not a.get("statusUrl") for a in CATALOG):
        raise ValueError("Unknown managed app")
    with open(os.environ["GITHUB_OUTPUT"], "a") as out:
        print("matrix=" + json.dumps(todo, separators=(",", ":")), file=out)
        print("errors=" + ",".join(failures), file=out)


def download_original(package, target, root):
    folder = root / "play"
    if target["source"] == "google-play":
        try:
            play(package, folder, "--expected-version", target["version"])
            return folder
        except (OSError, subprocess.SubprocessError):
            print("Play could not deliver this release. Checking the equivalent signed ARM64 mirror.")
    mirror = lookup(package, "apkpure")
    if mirror["versionName"] != target["version"] or (target["code"] and str(target["code"]) != str(mirror["versionCode"])):
        raise ValueError("The mirror does not have the selected release")
    folder = root / "mirror"
    subprocess.run(["bash", ROOT / "scripts/download-apkpure-xapk.sh", package, "arm64-v8a",
                    str(mirror["versionCode"]), target["version"], folder], check=True, timeout=360, env=child_env())
    return folder


def private_file(args, env, dest, expected):
    with dest.open("wb") as out:
        subprocess.run(["gh", *args], stdout=out, stderr=subprocess.PIPE, check=True, timeout=100, env=env)
    if digest(dest) != expected:
        raise ValueError("Private build input checksum mismatch")


def build_private(target, variant, root, output):
    spec = INPUTS[target["app"]]
    package = variant["packageName"]
    folder = download_original(package, target, root)
    manifest = json.loads((folder / "manifest.json").read_bytes())
    if manifest["package_name"] != package or manifest["version_name"] != target["version"]:
        raise ValueError("Source manifest disagrees with the intended release")
    apks = []
    for split in manifest["split_apks"]:
        name = split["file"]
        if not re.fullmatch(r"[\w.-]+\.apk", name, re.ASCII) or not (folder / name).is_file():
            raise ValueError("Invalid source APK filename")
        apk = folder / name
        if certificate(apk, 28) != spec["sourceSigner"]:
            raise ValueError("Source app signer changed")
        apks.append(apk)
    env = {**child_env(), "GH_TOKEN": os.environ["MODULE_READ_TOKEN"]}
    module = root / "module.apk"
    private_file(["release", "download", "workshop-input-" + spec["moduleSha256"][:20],
                  "-R", spec["repository"], "-p", "module.apk", "-O", "-"], env, module, spec["moduleSha256"])
    tools = root / "tools"
    tools.mkdir()
    packager = INPUTS["packager"]
    for name, fingerprint in packager["files"].items():
        route = f'repos/{packager["repository"]}/contents/tools/{name}?ref={packager["commit"]}'
        private_file(["api", route, "-H", "Accept: application/vnd.github.raw+json"], env, tools / name, fingerprint)
        (tools / name).chmod(0o700)
    key = root / "signing.jks"
    key.write_bytes(base64.b64decode("".join(os.environ["HOST_KEYSTORE"].split()), validate=True))
    key.chmod(0o600)
    signing = {**child_env(), "TEMPLATE_KS_PATH": str(key), "TEMPLATE_KS_PASS": "android",
               "TEMPLATE_KEY_ALIAS": "androiddebugkey", "TEMPLATE_KEY_PASS": "android",
               "TEMPLATE_CERT_SHA256": variant["signer"]}
    # This is the existing pinned private packager. Only its final APK set may be published.
    subprocess.run([sys.executable, tools / "package-lspatch.py", "--module", module, "--output", output, *apks],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=signing, timeout=480)
    return {"moduleDigest": spec["moduleSha256"], "moduleVersion": spec["moduleVersion"]}


def build_existing(target, variant, output):
    app = target["app"]
    if app == "anothereden":
        route, body = "/api/lspatch/build", dict(region=variant["region"], moduleVariant=variant["flavor"], moduleSource="main")
    else:
        route, body = f"/api/{app}/build", dict(versionName=target["version"], versionCode=str(target["code"] or ""))
    build = site_json(route, body)
    actual = build.get("moduleSha") or build.get("moduleShortSha")
    if not actual or not (actual.startswith(target["module"]) or target["module"].startswith(actual)):
        raise ValueError("Module release changed during preparation")
    if build.get("versionName") != target["version"]:
        raise ValueError("App release changed during preparation")
    deadline = time.monotonic() + 750
    status_route = build["statusUrl"]
    while time.monotonic() < deadline:
        status = site_json(status_route)
        if status.get("status") == "ready":
            url = site_url(status.get("downloadUrl") or build["downloadUrl"])
            subprocess.run(["curl", "--proto", "=https", "--proto-redir", "=https", "-fsSL", "--retry", "3",
                            "--max-time", "300", "-o", output, url], check=True)
            return {"moduleSha": target["module"]}
        if status.get("status") in ("failed", "error", "cancelled"):
            raise ValueError("Existing packaging workflow did not publish a bundle")
        time.sleep(7)
    raise TimeoutError("Background build did not finish in time")


def prepare(target, verify):
    app = next(app for app in CATALOG if app["id"] == target["app"])
    variant = next(v for v in app["variants"] if v["id"] == target["variant"])
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        output = root / "bundle.apks"
        metadata = (build_private(target, variant, root, output) if target["app"] in INPUTS
                    else build_existing(target, variant, output))
        receipt = verify_bundle(output, variant["packageName"], target["version"], variant["signer"], target["code"])
        if verify:
            print(f'Verified {target["variant"]} {receipt["version"]} ({receipt["versionCode"]}), {receipt["sizeBytes"]} bytes.')
            return
        published = publish_asset(variant["id"], output, {**receipt, **metadata})
        update_ready(variant["id"], published, target["previous"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["plan", "prepare"])
    args = parser.parse_args()
    verify = os.getenv("VERIFY_ONLY") == "true"
    if args.command == "plan":
        plan(os.getenv("SELECT_APP") or "all", verify)
    else:
        prepare(json.loads(os.environ["CANDIDATE"]), verify)
