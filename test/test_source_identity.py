import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts/catalog"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("source_identity", SCRIPTS / "verify_inputs.py")
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class SourceIdentityTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.names = ["base.apk", "split.apk"]
        for name in self.names:
            with zipfile.ZipFile(self.root / name, "w") as archive:
                archive.writestr("classes.dex", b"fixture")
        (self.root / "manifest.json").write_text(json.dumps({"split_apks": [{"file": n} for n in self.names]}))
        self.identities = {"base.apk": ("game.example", "1.0", 8, "", 28),
                           "split.apk": ("game.example", "1.0", 8, "config.arm64_v8a", 28)}

    def verify(self, certificates=None):
        with patch.object(source, "badging", side_effect=lambda p: self.identities[p.name]), \
             patch.object(source, "certificate", side_effect=lambda p, _: (certificates or {}).get(p.name, "publisher")):
            source.verify_source(self.root, "game.example", 8, "publisher")

    def test_foreign_split_signer_cannot_be_hidden_by_resigning(self):
        self.verify()
        with self.assertRaisesRegex(ValueError, "signer differs"):
            self.verify({"split.apk": "foreign"})

    def test_split_package_version_abi_and_duplicates_are_checked_before_signing(self):
        for identity in [("foreign", "1.0", 8, "config.arm64_v8a", 28),
                         ("game.example", "1.0", 7, "config.arm64_v8a", 28),
                         ("game.example", "1.0", 8, "config.x86", 28),
                         ("game.example", "1.0", 8, "", 28)]:
            self.identities["split.apk"] = identity
            with self.assertRaises(ValueError):
                self.verify()

    def test_prepatched_source_is_rejected(self):
        with zipfile.ZipFile(self.root / "split.apk", "a") as archive:
            archive.writestr("assets/lspatch/config.json", "{}")
        with self.assertRaisesRegex(ValueError, "unmodified publisher"):
            self.verify()
