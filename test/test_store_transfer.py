import base64
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts/catalog"
sys.path.insert(0, str(SCRIPTS))
import refresh


def load_downloader():
    spec = importlib.util.spec_from_file_location("store_delivery", Path(__file__).resolve().parents[1] / "scripts/download-play-apks.py")
    module = importlib.util.module_from_spec(spec)
    api = types.SimpleNamespace(get_delivery=None, get_details=None, purchase=None)
    with patch.dict(sys.modules, {"gplaydl.api": api, "gplaydl.auth": types.SimpleNamespace(ensure_auth=None)}):
        spec.loader.exec_module(module)
    return module


class StoreTransferTests(unittest.TestCase):
    def test_exact_apkpure_request_downloads_the_selected_code_without_another_latest_lookup(self):
        root = Path("fresh")
        selected = {"source": "apkpure", "version": "2.3.0", "code": "23446"}
        with patch.object(refresh, "mirror_version") as lookup, patch.object(refresh.subprocess, "run") as run:
            self.assertEqual(refresh.download_original("game.example", selected, root), root / "mirror")
            lookup.assert_not_called()
            self.assertEqual(run.call_args.args[0][2:],
                             ["game.example", "arm64-v8a", "23446", "2.3.0", root / "mirror"])

    def test_play_fallback_requires_a_matching_mirror_version_before_downloading(self):
        selected = {"source": "google-play", "version": "2.3.0", "code": None}
        with patch.object(refresh, "play", side_effect=OSError), \
             patch.object(refresh, "mirror_version", return_value={"versionName": "2.4.0", "versionCode": "23447"}), \
             patch.object(refresh.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "does not have the selected release"):
                refresh.download_original("game.example", selected, Path("fresh"))
            run.assert_not_called()

    def test_slow_source_resumes_only_unfinished_files_and_checks_play_hashes(self):
        downloader = load_downloader()
        body = b"original signed split"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rows = [{"url": "https://example.test/" + name, "raw": root / name, "final": root / name,
                     "gzipped": False, "cookies": [], "sha256": base64.urlsafe_b64encode(hashlib.sha256(body).digest()).decode()}
                    for name in ("base.apk", "split.apk")]
            def run(args, **options):
                if "--save-session" in args:
                    self.assertIn("--lowest-speed-limit=512K", args)
                    rows[0]["raw"].write_bytes(body)
                    Path(args[args.index("--save-session") + 1]).write_text(rows[1]["url"] + "\n  out=split.apk\n")
                    return subprocess.CompletedProcess(args, 5)
                saved = Path(args[args.index("--input-file") + 1]).read_text()
                self.assertNotIn("base.apk", saved)
                self.assertIn("split.apk", saved)
                self.assertFalse(any("--lowest-speed-limit" in arg for arg in args))
                rows[1]["raw"].write_bytes(body)
                return subprocess.CompletedProcess(args, 0)
            with patch.object(downloader.subprocess, "run", side_effect=run):
                downloader.download_with_aria(rows, root, fast_transfer=True)
                self.assertFalse(list(root.glob("aria2-*")))
                rows[1]["sha256"] = "wrong"
                with self.assertRaisesRegex(RuntimeError, "Play hash mismatch"):
                    downloader.download_with_aria(rows, root, fast_transfer=True)
