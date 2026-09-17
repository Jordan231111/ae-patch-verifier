const test = require('node:test');
const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const path = require('node:path');
const { readyApksAsset } = require('../api/_shared/release.js');

test('drafts and incomplete uploads cannot become downloadable APKS', () => {
  const asset = { name: 'test.apks', state: 'uploaded', size: 123 };
  assert.equal(readyApksAsset({ draft: false, assets: [asset] }), asset);
  for (const release of [null, { assets: [asset] }, { draft: true, assets: [asset] },
    { draft: false, assets: [{ ...asset, state: 'starter' }] },
    { draft: false, assets: [{ ...asset, size: 0 }] },
    { draft: false, assets: [{ ...asset, name: 'test.apk' }] }]) {
    assert.equal(readyApksAsset(release), null);
  }
});

test('publisher reconciles lost acknowledgements and retries only incomplete owned assets', () => {
  const result = spawnSync('python3', [path.join(__dirname, 'publish-release-test.py')], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stdout + result.stderr);
});

const fs = require('node:fs');
const vm = require('node:vm');
function handler(file, github, extras = {}) {
  const module = { exports: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', file), 'utf8'), {
    module, URL, process, Buffer,
    require: name => name === '../_shared/github.js' ? github
      : name === '../_shared/release.js' ? { readyApksAsset } : extras[name]
  });
  return module.exports;
}
function response() {
  return { headers: {}, setHeader(k,v) { this.headers[k] = v; }, end(text) { this.body = text; } };
}
test('status and download routes reject a listed but unfinished upload', async () => {
  const github = {
    config: () => ({ builderMode: 'github', githubOwner: 'owner', githubRepo: 'repo', githubWorkflow: 'build.yml', githubRef: 'main' }),
    githubJson: async (_, method, url) => url.includes('/releases/')
      ? { draft: true, assets: [{ id: 1, name: 'build.apks', state: 'starter', size: 0 }] }
      : { workflow_runs: [{ display_title: 'request-1', status: 'in_progress' }] },
    githubRequest: async () => assert.fail('An unfinished upload must never get a download redirect')
  };
  const status = response();
  await handler('api/lspatch/status.js', github)({ method: 'GET', url: '/?nonce=request-1' }, status);
  assert.equal(JSON.parse(status.body).status, 'running');
  assert.equal(JSON.parse(status.body).runStatus, 'publishing');
  const download = response();
  await handler('api/lspatch/download.js', github)({ method: 'GET', url: '/?nonce=request-1' }, download);
  assert.equal(download.statusCode, 404);
});

test('missing prebuilt returns a retryable error without dispatching module compilation', async () => {
  const github = {
    config: () => ({ builderMode: 'github', githubOwner: 'owner', githubRepo: 'repo' }),
    normalizeModuleSource: () => 'main', moduleFilenamePart: () => '',
    resolveModuleCommit: async () => ({ sha: 'a'.repeat(40), prebuilt: false }),
    githubJson: async () => assert.fail('No workflow should be dispatched without a prebuilt')
  };
  const build = handler('api/lspatch/build.js', github, {
    crypto: require('node:crypto'),
    '../_shared/googleplay.js': { resolveLatestPlayListing: async () => ({ versionName: 'future-version' }) },
    '../_shared/apkpure.js': {}
  });
  const res = response();
  await build({ method: 'POST', body: { region: 'global' } }, res);
  assert.equal(res.statusCode, 503);
  assert.match(JSON.parse(res.body).message, /precompiled module is not ready/);
});
