import importlib.util
import hashlib
import json
from base64 import b64decode
from pathlib import Path
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location("workshop_publish", Path(__file__).resolve().parents[1] / "scripts/catalog/publish.py")
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


class RawTransferTests(unittest.TestCase):
    def setUp(self):
        self.digest = "a" * 64
        self.record = {"version": "1.4.2", "requestKey": "b" * 64, "sha256": self.digest, "sizeBytes": 400}
        self.variant = "rustaxion-arm64-release"
        self.name = publish.bundle_name(self.variant, self.record["version"])

    def test_uploader_digest_must_match_before_announcement(self):
        env = {"GITHUB_RUN_ID": "10", "WORKSHOP_ARTIFACT_ID": "21", "WORKSHOP_ARTIFACT_DIGEST": "c" * 64}
        with patch.dict(publish.os.environ, env), patch.object(publish, "read_record") as read:
            with self.assertRaises(ValueError): publish.announce(self.variant, self.record)
            read.assert_not_called()

    def test_clean_runner_has_every_hash_locked_public_wheel_and_archive_tool(self):
        root = Path(__file__).resolve().parents[1]
        tools = json.loads((root / "scripts/catalog/inputs.json").read_bytes())
        wheels = root / ".github/play-client"
        lock = (wheels / "requirements.txt").read_bytes()
        self.assertEqual(hashlib.sha256(lock).hexdigest(), tools["playClientLock"])
        expected = {line.split("--hash=sha256:")[1] for line in lock.decode().splitlines() if line and not line.startswith("#")}
        self.assertEqual(expected, {hashlib.sha256(f.read_bytes()).hexdigest() for f in wheels.glob("*.whl")})
        archive, = (root / "scripts/catalog/tools").glob("zipmerge_*.deb")
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), tools["packager"]["zipmergeSha256"])

    def test_announcement_preserves_the_winning_claim_and_uses_compare_and_swap(self):
        ticket = {"key": self.record["requestKey"], "version": "1.4.2", "nonce": "my-request", "time": 17}
        env = {"WORKSHOP_NONCE": ticket["nonce"], "GITHUB_RUN_ID": "10",
               "WORKSHOP_ARTIFACT_ID": "21", "WORKSHOP_ARTIFACT_DIGEST": self.digest}
        with patch.dict(publish.os.environ, env), patch.object(publish, "api") as api:
            with patch.object(publish, "read_record", return_value=({**ticket, "nonce": "new-request"}, "revision")):
                publish.announce(self.variant, self.record)
                api.assert_not_called()
            with patch.object(publish, "read_record", return_value=(ticket.copy(), "revision")):
                publish.announce(self.variant, self.record)
                path, method, body = api.call_args.args
                self.assertEqual((path, method, body["sha"]), ("contents/requests/" + self.variant + ".json", "PUT", "revision"))
                saved = json.loads(b64decode(body["content"]))
                self.assertEqual(saved["time"], 17)
                self.assertEqual(saved["transfer"]["artifactId"], 21)
                self.assertEqual(saved["transfer"]["sha256"], self.digest)
