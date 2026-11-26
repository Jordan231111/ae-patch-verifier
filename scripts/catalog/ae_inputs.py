"""Verify the selected AE recipe and authorize its host helper before signing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def recipe(inputs):
    preparation = inputs["aeLogin"]["preparation"]
    return "/".join([inputs["anotherEdenPatcher"]["sha256"], inputs["sharedSigning"]["certificateSha256"],
                     inputs["aeLogin"]["companion"]["sha256"], preparation["sha256"],
                     f'prepare-v{preparation["schema"]}'])


def authorize(module, companion, inputs, *, root=ROOT, expected_recipe=None,
              expected_module_digest=None, module_commit=None):
    selected_recipe = recipe(inputs)
    if expected_recipe and expected_recipe != selected_recipe:
        raise ValueError("The requested AE recipe changed before this build started")
    module_digest = digest(module)
    if expected_module_digest and module_digest != expected_module_digest.removeprefix("sha256:"):
        raise ValueError("The module differs from the requested release asset")
    patcher = inputs["anotherEdenPatcher"]
    if digest(root / patcher["file"]) != patcher["sha256"]:
        raise ValueError("The checkout patcher differs from the selected AE recipe")
    if digest(companion) != inputs["aeLogin"]["companion"]["sha256"]:
        raise ValueError("The companion differs from the selected AE recipe")
    preparation = inputs["aeLogin"]["preparation"]
    helper = (root / preparation["file"]).resolve()
    if not helper.is_relative_to(root.resolve()) or helper.name != "PrepareMicrog.java":
        raise ValueError("Invalid reviewed host helper path")
    if preparation["schema"] != 1 or preparation.get("trustedCompanionArgument") is not True:
        raise ValueError("Unsupported reviewed host helper contract")
    if digest(helper) != preparation["sha256"]:
        raise ValueError("The reviewed host helper differs from the selected AE recipe")
    with zipfile.ZipFile(module) as archive:
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise ValueError("Duplicate module archive entries")
        name = "assets/host-tools/PrepareMicrog.java"
        prepared = name in archive.namelist()
        if not prepared:
            raise ValueError("Module has no reviewed host preparation contract")
        if prepared:
            if archive.namelist().count(name) != 1 or archive.read(name) != helper.read_bytes():
                raise ValueError("The module host helper is not the independently reviewed source")
            contract_name = "assets/host-tools/contract.json"
            if contract_name not in archive.namelist():
                raise ValueError("Module has no reviewed host preparation contract")
            if contract_name in archive.namelist():
                contract = json.loads(archive.read(contract_name))
                expected_contract = {"schema": 1, "helper": helper.name,
                    "helperSha256": preparation["sha256"], "trustedCompanionArgument": True,
                    "modulePackage": "com.jordan.aepcdtrace"}
                if contract != expected_contract:
                    raise ValueError("Unsupported module host preparation contract")
        metadata = "assets/module-build.json"
        if module_commit:
            if metadata not in archive.namelist():
                raise ValueError("Module build provenance is missing")
            build = json.loads(archive.read(metadata))
            if build.get("moduleCommit") != module_commit or build.get("worktreeDirty") is not False:
                raise ValueError("Module build provenance does not match the selected source revision")
    return {"schema": 1, "recipe": selected_recipe, "moduleSha256": module_digest,
            "patcherSha256": patcher["sha256"], "loaderBuild": patcher["build"],
            "companionSha256": inputs["aeLogin"]["companion"]["sha256"],
            "helperSha256": preparation["sha256"] if prepared else None,
            "prepared": prepared, "helper": str(helper) if prepared else None}


def verify_bundle_inputs(bundle, receipt, candidate, inputs):
    if receipt.get("schema") != 1 or receipt.get("recipe") != recipe(inputs):
        raise ValueError("The AE input receipt has a different recipe")
    if candidate.get("recipe") != receipt["recipe"]:
        raise ValueError("The completed build has a different requested AE recipe")
    if candidate.get("moduleDigest") != receipt.get("moduleSha256"):
        raise ValueError("The completed build has a different requested module artifact")
    with zipfile.ZipFile(bundle) as outer:
        for name in outer.namelist():
            if not name.endswith(".apk"):
                continue
            with outer.open(name) as member, zipfile.ZipFile(member) as base:
                config_name = "assets/lspatch/config.json"
                if config_name not in base.namelist():
                    continue
                config = json.loads(base.read(config_name))
                if config.get("useManager") or config.get("sigBypassLevel") != 2:
                    raise ValueError("The delivered host has a different LSPatch mode")
                if config["lspConfig"]["VERSION_CODE"] != inputs["anotherEdenPatcher"]["build"]:
                    raise ValueError("The delivered host has a different LSPatch loader")
                module = base.read("assets/lspatch/modules/com.jordan.aepcdtrace.apk")
                if hashlib.sha256(module).hexdigest() != receipt["moduleSha256"]:
                    raise ValueError("The delivered embedded module differs from its input receipt")
                return
    raise ValueError("The delivered AE bundle has no embedded LSPatch host")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--companion", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    inputs = json.loads((ROOT / "scripts/catalog/inputs.json").read_bytes())
    candidate = json.loads(os.environ.get("CANDIDATE") or "null")
    if candidate and not candidate.get("recipe"):
        raise ValueError("The catalog request has no checked AE recipe")
    expected_digest = candidate.get("moduleDigest") if candidate else os.getenv("MODULE_ASSET_DIGEST")
    if candidate and not expected_digest:
        raise ValueError("The catalog request has no checked module artifact digest")
    receipt = authorize(args.module, args.companion, inputs,
                        expected_recipe=candidate["recipe"] if candidate else os.getenv("EXPECTED_RECIPE"),
                        expected_module_digest=expected_digest, module_commit=os.getenv("MODULE_SHA"))
    args.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
    print("Verified AE patcher, companion, module artifact and reviewed host helper")


if __name__ == "__main__":
    main()
