import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("ae_inputs", Path(__file__).resolve().parents[1] / "scripts/catalog/ae_inputs.py")
ae = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ae)


class AeInputsTests(unittest.TestCase):
    def test_api_and_workflow_agree_on_the_recipe_including_the_signer(self):
        root = Path(__file__).resolve().parents[1]
        inputs = json.loads((root / 'scripts/catalog/inputs.json').read_text())
        api_recipe = subprocess.check_output(['node', '-e',
            'process.stdout.write(require("./api/_shared/recipes").recipe("anothereden"))'], cwd=root, text=True)
        self.assertEqual(api_recipe, ae.recipe(inputs))
        inputs['sharedSigning']['certificateSha256'] = '0' * 64
        self.assertNotEqual(api_recipe, ae.recipe(inputs))

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.helper = self.root / "PrepareMicrog.java"
        self.helper.write_bytes(b"class PrepareMicrog {}")
        self.patcher = self.root / "patcher.jar"
        self.patcher.write_bytes(b"reviewed patcher")
        self.companion = self.root / "companion.apk"
        self.companion.write_bytes(b"selected companion")
        self.inputs = {"sharedSigning": {"certificateSha256": "c" * 64}, "anotherEdenPatcher": {"sha256": ae.digest(self.patcher), "file": self.patcher.name, "build": 487},
            "aeLogin": {"companion": {"sha256": ae.digest(self.companion)},
                "preparation": {"schema": 1, "file": self.helper.name,
                    "sha256": ae.digest(self.helper), "trustedCompanionArgument": True}}}
        self.module = self.root / "module.apk"
        self.contract = {"schema": 1, "helper": self.helper.name, "helperSha256": ae.digest(self.helper),
            "trustedCompanionArgument": True, "modulePackage": "com.jordan.aepcdtrace"}
        self.write_module()

    def write_module(self, helper=None, contract=True):
        with zipfile.ZipFile(self.module, "w") as archive:
            archive.writestr("assets/host-tools/PrepareMicrog.java", helper or self.helper.read_bytes())
            if contract:
                archive.writestr("assets/host-tools/contract.json", json.dumps(self.contract))

    def authorize(self, **kwargs):
        return ae.authorize(self.module, self.companion, self.inputs, root=self.root, **kwargs)

    def test_current_contract_is_required(self):
        self.assertTrue(self.authorize()["prepared"])
        self.write_module(contract=False)
        with self.assertRaisesRegex(ValueError, "no reviewed host preparation contract"):
            self.authorize()

    def test_selected_commit_requires_clean_matching_provenance(self):
        with self.assertRaisesRegex(ValueError, "provenance is missing"):
            self.authorize(module_commit="a" * 40)
        for commit, dirty, valid in [("a" * 40, False, True), ("b" * 40, False, False), ("a" * 40, True, False)]:
            self.write_module()
            with zipfile.ZipFile(self.module, "a") as archive:
                archive.writestr("assets/module-build.json", json.dumps({"moduleCommit": commit, "worktreeDirty": dirty}))
            if valid:
                self.assertTrue(self.authorize(module_commit="a" * 40)["prepared"])
            else:
                with self.assertRaisesRegex(ValueError, "provenance does not match"):
                    self.authorize(module_commit="a" * 40)

    def test_self_consistent_module_helper_cannot_authorize_its_own_code(self):
        unreviewed = b"class PrepareMicrog { /* changed code */ }"
        self.contract["helperSha256"] = hashlib.sha256(unreviewed).hexdigest()
        self.write_module(helper=unreviewed)
        with self.assertRaisesRegex(ValueError, "independently reviewed"):
            self.authorize()

    def test_unknown_contracts_and_changed_checkout_tools_are_rejected(self):
        self.contract["schema"] = 2
        self.write_module()
        with self.assertRaisesRegex(ValueError, "Unsupported module"):
            self.authorize()
        self.write_module(contract=False)
        self.helper.write_bytes(b"changed checkout source")
        with self.assertRaisesRegex(ValueError, "reviewed host helper differs"):
            self.authorize()

    def test_a_queued_build_cannot_use_a_new_deployments_recipe_or_replaced_asset(self):
        with self.assertRaisesRegex(ValueError, "recipe changed"):
            self.authorize(expected_recipe="old deployment")
        with self.assertRaisesRegex(ValueError, "module differs"):
            self.authorize(expected_module_digest="a" * 64)
        expected = ae.recipe(self.inputs)
        self.companion.write_bytes(b"replaced companion")
        with self.assertRaisesRegex(ValueError, "companion differs"):
            self.authorize(expected_recipe=expected)

    def test_finished_bundle_must_embed_the_selected_module_and_loader(self):
        receipt = self.authorize()
        candidate = {"recipe": receipt["recipe"], "moduleDigest": receipt["moduleSha256"]}
        bundle = self.root / "bundle.apks"

        def write(module, build=487):
            nested = io.BytesIO()
            with zipfile.ZipFile(nested, "w") as base:
                base.writestr("assets/lspatch/config.json", json.dumps({"useManager": False,
                    "sigBypassLevel": 2, "lspConfig": {"VERSION_CODE": build}}))
                base.writestr("assets/lspatch/modules/com.jordan.aepcdtrace.apk", module)
            with zipfile.ZipFile(bundle, "w") as outer:
                outer.writestr("base.apk", nested.getvalue())

        write(self.module.read_bytes())
        ae.verify_bundle_inputs(bundle, receipt, candidate, self.inputs)
        write(b"different module")
        with self.assertRaisesRegex(ValueError, "embedded module differs"):
            ae.verify_bundle_inputs(bundle, receipt, candidate, self.inputs)
        write(self.module.read_bytes(), 486)
        with self.assertRaisesRegex(ValueError, "different LSPatch loader"):
            ae.verify_bundle_inputs(bundle, receipt, candidate, self.inputs)
        candidate["recipe"] = "another deployment"
        with self.assertRaisesRegex(ValueError, "different requested AE recipe"):
            ae.verify_bundle_inputs(bundle, receipt, candidate, self.inputs)
