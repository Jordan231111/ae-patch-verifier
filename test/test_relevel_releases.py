"""Release/index state transitions, including failures which must never publish."""
import argparse
from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('relevel_releases', Path(__file__).parents[1] / 'scripts/relevel/releases.py')
releases = importlib.util.module_from_spec(spec)
spec.loader.exec_module(releases)


class ReleaseStateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.version = {'package_name': releases.PACKAGE, 'version_code': 44, 'version_name': '1.1.3'}
        self.module = 'a' * 40
        self.observed = releases.observed(self.version, self.module)
        self.key = releases.digest({'observed': self.observed, 'recoverySchema': releases.RECOVERY_SCHEMA})
        self.version_path = self.root / 'version.json'
        self.version_path.write_text(json.dumps(self.version))
        self.plan = self.root / 'plan.json'
        self.args = argparse.Namespace(version=self.version_path, plan=self.plan, force=False)
        self.environment = patch.dict(os.environ, {'GITHUB_REPOSITORY':'owner/public', 'GITHUB_SHA':'b'*40, 'GITHUB_RUN_ID':'123'})
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def inspect(self, state, *, force=False):
        self.args.force = force
        def api(path, **kwargs):
            if path == 'repos/owner/public': return {'private':False}
            if '/releases/assets/' in path: return {'state':'uploaded','size':10}
            if '/releases/tags/relevel-inputs-' in path: return {'assets':[{'name':'recovered.tar','state':'uploaded'}]}
            raise AssertionError(path)
        with patch.object(releases,'api',side_effect=api), patch.object(releases,'latest_module',return_value=self.module), \
             patch.object(releases,'index',return_value=(None,deepcopy(state))), patch.object(releases,'write_index') as write, \
             patch.object(releases,'emit') as emit:
            releases.inspect(self.args)
            return emit.call_args.args[0], write.call_args.args[0]

    def ready(self):
        return {'schema':1,'packageName':releases.PACKAGE,'state':'ready','observed':self.observed,
                'ready':{**self.observed,'validated':True,'cacheKey':self.key,'sizeBytes':10,'releaseAssetId':3,
                         'recoveredTag':'relevel-inputs-'+'c'*64}}

    def test_unchanged_inputs_only_refresh_metadata(self):
        output, state = self.inspect(self.ready())
        self.assertEqual(output['changed'],'false')
        self.assertEqual(state['state'],'ready')
        self.assertIn('checkedAt',state)

    def test_module_changes_reuse_recovered_inputs_but_hide_old_bundle(self):
        old = self.ready()
        self.module = 'd'*40
        output, state = self.inspect(old)
        self.assertEqual(output['changed'],'true')
        self.assertEqual(output['recovered_tag'],old['ready']['recoveredTag'])
        self.assertEqual(state['state'],'preparing')
        self.assertNotEqual(state['ready']['moduleSha'],state['observed']['moduleSha'])

    def test_same_failing_input_stops_heavy_polling_until_a_manual_retry(self):
        failed = self.ready()
        failed.update(state='requires_review',failedKey=self.key)
        self.assertEqual(self.inspect(failed)[0]['changed'],'false')
        self.assertEqual(self.inspect(failed,force=True)[0]['changed'],'true')

    def test_private_repositories_cannot_silently_enable_paid_polling(self):
        with patch.object(releases,'api',return_value={'private':True}), patch.object(releases,'latest_module') as module:
            with self.assertRaisesRegex(RuntimeError,'public'): releases.inspect(self.args)
            module.assert_not_called()

    def test_mismatched_validation_never_reaches_publication(self):
        bundle = self.root/'ReLevel-test.apks'
        bundle.write_bytes(b'candidate')
        checksum = releases.file_digest(bundle)
        self.plan.write_text(json.dumps({'observed':self.observed,'cacheKey':self.key}))
        metadata = {'bundleSha256':checksum,'certificateSha256':releases.CERT,'packageName':releases.PACKAGE,
                    'moduleSourceSha':self.module,'versionCode':44,'versionName':'1.1.3'}
        report = {'bundle_sha256':checksum,'ok':True,'home_resume':True,'runtime':'redroid'}
        args = argparse.Namespace(plan=self.plan,bundle=bundle,metadata=self.root/'bundle.json',report=self.root/'report.json',version=self.version_path,recovered=None)
        for changed in [('report','home_resume',False),('report','bundle_sha256','f'*64),
                        ('metadata','moduleSourceSha','f'*40),('metadata','certificateSha256','f'*64)]:
            current = {'report':deepcopy(report),'metadata':deepcopy(metadata)}
            current[changed[0]][changed[1]] = changed[2]
            args.metadata.write_text(json.dumps(current['metadata']))
            args.report.write_text(json.dumps(current['report']))
            with patch.object(releases,'api') as api, patch.object(releases,'release_asset') as publish:
                with self.assertRaises(RuntimeError): releases.publish(args)
                api.assert_not_called()
                publish.assert_not_called()


if __name__ == '__main__': unittest.main()
