import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("allrelic_builder", Path(__file__).resolve().parents[1] / "scripts/allrelic/build_apks.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class AllRelicInputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        for name in ["com.AllRelic.apk", "config.arm64_v8a.apk"]:
            (self.source / name).write_bytes(b"apk")
        self.manifest = dict(package_name="com.AllRelic", version_code="148", version_name="1.2.4",
                             split_apks=[dict(id="base", file="com.AllRelic.apk"),
                                         dict(id="config.arm64_v8a", file="config.arm64_v8a.apk")])

    def validate(self, manifest=None):
        return builder.source_splits(manifest or self.manifest, self.source, "148", "1.2.4")

    def test_complete_split_set_is_accepted(self):
        self.assertEqual(len(self.validate()), 2)

    def test_changed_version_package_or_abi_is_rejected(self):
        for key, value in [("version_code", "149"), ("version_name", "new"), ("package_name", "other")]:
            candidate = dict(self.manifest, **{key: value})
            with self.assertRaises(ValueError):
                self.validate(candidate)
        candidate = copy.deepcopy(self.manifest)
        candidate["split_apks"][1]["id"] = "config.armeabi_v7a"
        with self.assertRaises(ValueError):
            self.validate(candidate)

    def test_duplicate_missing_and_unsafe_splits_are_rejected(self):
        for split in [dict(id="base", file="config.arm64_v8a.apk"),
                      dict(id="config.arm64_v8a", file="../outside.apk"),
                      dict(id="config.arm64_v8a", file="missing.apk")]:
            candidate = copy.deepcopy(self.manifest)
            candidate["split_apks"][1] = split
            with self.assertRaises(ValueError):
                self.validate(candidate)
        (self.source / "unlisted.apk").write_bytes(b"apk")
        with self.assertRaisesRegex(ValueError, "Unlisted"):
            self.validate()

    def test_original_signer_is_pinned_without_confusing_the_source_stamp(self):
        pin = "a" * 64
        output = "V3.0 Signer: certificate SHA-256 digest: " + pin + "\nSource Stamp Signer: certificate SHA-256 digest: " + "b" * 64
        with patch.object(builder, "run", return_value=output):
            builder.certificate("apksigner", "base.apk", pin)
            with self.assertRaises(ValueError):
                builder.certificate("apksigner", "base.apk", "c" * 64)
        with patch.object(builder, "run", return_value=output + "\nSigner #2 certificate SHA-256 digest: " + "d" * 64):
            with self.assertRaises(ValueError):
                builder.certificate("apksigner", "base.apk", pin)


if __name__ == "__main__":
    unittest.main()
