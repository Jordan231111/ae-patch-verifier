import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('publisher', Path(__file__).parents[1] / 'scripts/publish-release.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)


class Fake(module.Publisher):
    def __init__(self, path, fail_first=False, stale=False):
        super().__init__('owner/repo', 'test')
        self.asset = dict(id=2, name=path.name, state='uploaded', size=path.stat().st_size,
                          digest='sha256:' + hashlib.sha256(path.read_bytes()).hexdigest())
        self.release = dict(id=1, draft=True, upload_url='https://uploads.github.com/example{?name}',
                            assets=[dict(self.asset, state='starter')] if stale else [], html_url='https://github.com/example')
        self.fail_first, self.uploads, self.deleted = fail_first, 0, 0

    def api(self, method, path, data=None):
        if method == 'DELETE': self.deleted += 1; self.release['assets'] = []; return None
        if method == 'PATCH':
            assert module.ready_asset(self.release['assets'], self.asset['name'], self.asset['size'], self.asset['digest'])
            self.release['draft'] = False
        return self.release['assets'] if path.endswith('/assets') else self.release

    def upload(self, url, path):
        self.uploads += 1
        self.release['assets'] = [dict(self.asset, state='starter')] if self.fail_first and self.uploads == 1 else [self.asset]
        return None  # simulate a lost response, including after successful upload


class Publishing(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'test.apks'; self.path.write_bytes(b'APKS test')

    def test_lost_success_is_not_replayed(self):
        p = Fake(self.path); p.publish(self.path, 'test', 'sha', 'title', 'body')
        self.assertEqual(p.uploads, 1); self.assertEqual(p.deleted, 0); self.assertFalse(p.release['draft'])

    def test_partial_is_deleted_before_retry(self):
        p = Fake(self.path, fail_first=True); p.publish(self.path, 'test', 'sha', 'title', 'body')
        self.assertEqual(p.uploads, 2); self.assertEqual(p.deleted, 1)

    def test_existing_incomplete_asset_is_reconciled(self):
        p = Fake(self.path, stale=True); p.publish(self.path, 'test', 'sha', 'title', 'body')
        self.assertEqual(p.uploads, 1); self.assertEqual(p.deleted, 1)

    def test_different_published_content_is_not_overwritten(self):
        p = Fake(self.path, stale=True); p.release['draft'] = False
        with self.assertRaises(RuntimeError): p.publish(self.path, 'test', 'sha', 'title', 'body')
        self.assertEqual(p.uploads, 0); self.assertEqual(p.deleted, 0)


if __name__ == '__main__': unittest.main()
