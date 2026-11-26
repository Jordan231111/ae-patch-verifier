"""Prepare a visitor-requested APK set and publish its verified release receipt."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import base64
import errno
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

from bundle import certificate, digest, verify_bundle
from publish import bundle_name, delivery_dir, ready_file
from ae_inputs import verify_bundle_inputs


ROOT = Path(__file__).resolve().parents[2]
CATALOG = json.loads((ROOT / "catalog.json").read_bytes())["apps"]
INPUTS = json.loads((ROOT / "scripts/catalog/inputs.json").read_bytes())


def mirror_version(package):
    return json.loads(subprocess.check_output(
        ["node", ROOT / "scripts/catalog/lookup.js", package], timeout=85))


def play(package, folder, *options):
    # Each subprocess gets only the credentials it uses. Its diagnostic output stays on the runner.
    result = subprocess.run([sys.executable, ROOT / "scripts/download-play-apks.py", "--package", package,
                    "--architecture", "arm64-v8a", "--account-email", os.getenv("PLAY_EMAIL", ""),
                    "--metadata-source", "google-play", "--fast-transfer", "--output", folder, *options],
                   check=True, capture_output=True, timeout=330,
                   env=child_env("GPLAYDL_API_KEY"))
    # Forward only numeric timing lines; store credentials and signed URLs stay on the runner.
    for timing in re.findall(rb"^\[source\] (?:Transferred \d+ Play files|Play delivery metadata ready) in \d+\.\d+s\.$",
                             result.stdout, re.M):
        print(timing.decode("ascii"), flush=True)


def child_env(*extra):
    names = {"PATH", "HOME", "LANG", "TMPDIR", "ANDROID_HOME", "ANDROID_SDK_ROOT", "JAVA_HOME", *extra}
    return {name: value for name, value in os.environ.items() if name in names}


def download_original(package, target, root):
    folder = root / "play"
    if target["source"] == "google-play":
        try:
            play(package, folder, "--expected-version", target["version"])
            return folder
        except (OSError, subprocess.SubprocessError):
            print("Play could not deliver this release. Checking the equivalent signed ARM64 mirror.")
    mirror = mirror_version(package)
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


def download_companion(destination):
    selected = INPUTS["aeLogin"]["companion"]
    with urllib.request.urlopen(selected["url"], timeout=100) as response, destination.open("wb") as stream:
        shutil.copyfileobj(response, stream)
    if digest(destination) != selected["sha256"]:
        raise ValueError("The companion differs from the selected recipe")
    return ["--google-companion", destination]


def verified_originals(package, target, root, source_signer):
    folder = download_original(package, target, root)
    manifest = json.loads((folder / "manifest.json").read_bytes())
    if manifest.get("expansions") or any(folder.rglob("*.obb")):
        raise ValueError("The source has expansion files outside the installable APK set")
    if manifest["package_name"] != package or manifest["version_name"] != target["version"]:
        raise ValueError("Source manifest disagrees with the intended release")
    apks = []
    for split in manifest["split_apks"]:
        name = split["file"]
        if not re.fullmatch(r"[\w.-]+\.apk", name, re.ASCII) or not (folder / name).is_file():
            raise ValueError("Invalid source APK filename")
        apk = folder / name
        if certificate(apk, 28) != source_signer:
            raise ValueError("Source app signer changed")
        apks.append(apk)
    return apks


def build_private(target, variant, root, output):
    started = time.monotonic()
    spec = INPUTS[target["app"]]
    package = variant["packageName"]
    env = {**child_env(), "GH_TOKEN": os.environ["MODULE_READ_TOKEN"]}
    module = root / "module.apk"
    tools = root / "tools"
    tools.mkdir()
    packager = spec["packager"]
    selected_recipe = [INPUTS["anotherEdenPatcher"]["sha256"],
                       INPUTS["sharedSigning"]["certificateSha256"], packager["commit"]]
    companion_args = []
    if target["app"] == "epicconquest":
        selected_recipe.append(INPUTS["aeLogin"]["companion"]["sha256"])
    selected_recipe = "/".join(selected_recipe)
    if target.get("recipe") != selected_recipe:
        raise ValueError("The requested private recipe changed before this build started")
    with ThreadPoolExecutor(max_workers=4) as pool:
        originals = pool.submit(verified_originals, package, target, root, spec["sourceSigner"])
        prebuilt = pool.submit(private_file, ["release", "download", spec["moduleTag"],
                              "-R", spec["repository"], "-p", spec["moduleAsset"], "-O", "-"], env, module, spec["moduleSha256"])
        companion = pool.submit(download_companion, root / "companion.apk") if target["app"] == "epicconquest" else None
        downloads = []
        for name, fingerprint in packager["files"].items():
            route = f'repos/{packager["repository"]}/contents/tools/{name}?ref={packager["commit"]}'
            downloads.append(pool.submit(private_file, ["api", route, "-H", "Accept: application/vnd.github.raw+json"],
                                         env, tools / name, fingerprint))
        for download in downloads:
            download.result()
        # Both producer tools verify and reuse this exact offline patcher.
        patcher = INPUTS["anotherEdenPatcher"]
        if digest(ROOT / patcher["file"]) != patcher["sha256"]:
            raise ValueError("The checkout patcher differs from the selected recipe")
        patcher_dir = root / "artifacts/lspatch"
        patcher_dir.mkdir(parents=True)
        shutil.copyfile(ROOT / patcher["file"], patcher_dir / Path(patcher["file"]).name)
        helper = tools / "download-lspatch.sh"
        helper.chmod(0o700)
        subprocess.run([helper], env=child_env(), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True, timeout=100)
        prebuilt.result()
        apks = originals.result()
        if companion:
            companion_args = companion.result()
    prepared = time.monotonic()
    print(f"Original app and checked private inputs ready in {prepared - started:.2f}s.", flush=True)
    signing = {**child_env(), **{name: os.environ[name] for name in (
        "TEMPLATE_KS_PATH", "TEMPLATE_KS_PASS", "TEMPLATE_KEY_ALIAS", "TEMPLATE_KEY_PASS", "TEMPLATE_CERT_SHA256")}}
    if signing["TEMPLATE_CERT_SHA256"] != INPUTS["sharedSigning"]["certificateSha256"]:
        raise ValueError("Configured signer differs from the selected shared identity")
    # This is the existing pinned private packager. Only its final APK set may be published.
    archive_args = ["--fast-archive"] if packager["fastArchive"] else []
    subprocess.run([sys.executable, tools / "package-lspatch.py", "--module", module, "--output", output, *archive_args, *companion_args, *apks],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=signing, timeout=480)
    print(f"Signed bundle produced in {time.monotonic() - prepared:.2f}s.", flush=True)
    return {"moduleDigest": spec["moduleSha256"], "moduleVersion": spec["moduleVersion"],
            "recipe": selected_recipe}


def prepare(target, existing):
    app = next(a for a in CATALOG if a["id"] == target["app"] and not a.get("statusUrl"))
    variant = next(v for v in app["variants"] if v["id"] == target["variant"])
    if not re.fullmatch(r"[a-f0-9]{64}", target["key"]):
        raise ValueError("Invalid release identity")
    current = ready_file()[0].get(variant["id"], variant)
    if current.get("requestKey") == target["key"] and not os.getenv("VERIFY_ONLY") == "true":
        print("This release is already available.")
        return
    if current.get("sha256") != target["previous"]:
        raise ValueError("The requested release has already changed")
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        if existing:
            if target["module"] != os.environ["MODULE_SHA"] or app["id"] in INPUTS:
                raise ValueError("The build does not match the requested module identity")
            output = Path(os.environ["OUT"])
            metadata = {"moduleSha": target["module"], "recipe": target["recipe"]}
            if app["id"] == "anothereden":
                input_receipt = json.loads(Path(os.environ["AE_INPUT_RECEIPT"]).read_bytes())
                verify_bundle_inputs(output, input_receipt, target, INPUTS)
                metadata.update(moduleDigest=input_receipt["moduleSha256"], recipe=input_receipt["recipe"],
                                companionDigest=input_receipt["companionSha256"], loaderBuild=input_receipt["loaderBuild"])
        else:
            if target["module"] != INPUTS[app["id"]]["moduleSha256"]:
                raise ValueError("The requested private input has changed")
            output = root / "bundle.apks"
            metadata = build_private(target, variant, root, output)
        verify_args = (output, variant["packageName"], target["version"],
                       INPUTS["sharedSigning"]["certificateSha256"], target["code"])
        started = time.monotonic()
        receipt = verify_bundle(*verify_args)
        print(f"Finished file and Android signatures verified in {time.monotonic() - started:.2f}s.", flush=True)
        if receipt["versionCode"] < current["versionCode"]:
            raise ValueError("The delivered app is older than the published version")
        if os.getenv("VERIFY_ONLY") == "true":
            print(f'Verified {variant["id"]} {receipt["version"]} ({receipt["versionCode"]}).')
            return
        directory = delivery_dir()
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        final = directory / bundle_name(variant["id"], receipt["version"])
        # Only the verified bundle is in the upload step's exact file path.
        try:
            os.link(output, final)
        except OSError as error:
            if error.errno != errno.EXDEV:
                raise
            shutil.copyfile(output, final)
        record = {**receipt, **metadata, "requestKey": target["key"]}
        (directory / "receipt.json").write_text(json.dumps({
            "variant": variant["id"], "previous": target["previous"], "record": record
        }))
        with open(os.environ["GITHUB_OUTPUT"], "a") as out:
            print(f"file={final}", file=out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["private", "existing"])
    args = parser.parse_args()
    prepare(json.loads(os.environ["CANDIDATE"]), args.command == "existing")
