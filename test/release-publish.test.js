const test = require('node:test');
const assert = require('node:assert/strict');
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

const fs = require('node:fs');
const vm = require('node:vm');
function handler(file, github, extras = {}) {
  const module = { exports: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', file), 'utf8'), {
    module, URL, process, Buffer,
    require: name => name === '../_shared/github.js' ? github
      : name === '../_shared/release.js' ? { readyApksAsset }
      : name === '../_shared/build-artifact.js' ? extras[name] : extras[name]
  });
  return module.exports;
}
function response() {
  return { headers: {}, setHeader(k,v) { this.headers[k] = v; }, end(text) { this.body = text; } };
}
test('status and download routes reject an unfinished legacy upload', async () => {
  const github = {
    config: () => ({ builderMode: 'github', githubOwner: 'owner', githubRepo: 'repo', githubWorkflow: 'build.yml', githubRef: 'main' }),
    githubJson: async () => ({ draft: true, assets: [{ id: 1, name: 'build.apks', state: 'starter', size: 0 }] }),
    githubRequest: async () => assert.fail('An unfinished upload must never get a download redirect')
  };
  const extra = { '../_shared/build-artifact.js': {
    findBuildRun: async () => ({ id: 10, status: 'in_progress' }), findBuildArtifact: async () => null
  } };
  const status = response();
  await handler('api/lspatch/status.js', github, extra)({ method: 'GET', url: '/?nonce=request-1' }, status);
  assert.equal(JSON.parse(status.body).status, 'running');
  const download = response();
  await handler('api/lspatch/download.js', github, extra)({ method: 'GET', url: '/?nonce=request-1' }, download);
  assert.equal(download.statusCode, 404);
});

test('raw APKS artifacts keep their filename and redirect without repackaging', async () => {
  const asset = { id: 20, name: 'AnotherEden_test_request-1.apks', expired: false, size_in_bytes: 123 };
  const github = {
    config: () => ({ builderMode: 'github', githubOwner: 'owner', githubRepo: 'repo' }),
    githubRequest: async (_, request) => {
      assert.equal(request.apiPath, '/repos/owner/repo/actions/artifacts/20/zip');
      return { status: 302, headers: { location: 'https://storage.example/signed.apks' } };
    }
  };
  const extra = { '../_shared/build-artifact.js': {
    findBuildRun: async () => ({ id: 10 }), findBuildArtifact: async () => asset
  } };
  const status = response();
  await handler('api/lspatch/status.js', github, extra)({ method: 'GET', url: '/?nonce=request-1' }, status);
  assert.equal(JSON.parse(status.body).downloadUrl, '/api/lspatch/download?nonce=request-1&runId=10');
  const download = response();
  await handler('api/lspatch/download.js', github, extra)({ method: 'GET', url: '/?nonce=request-1' }, download);
  assert.equal(download.statusCode, 302); assert.equal(download.headers['x-asset-name'], asset.name);
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

test('artifact selection rejects expired, empty and other-request files', () => {
  const { readyApksArtifact } = require('../api/_shared/build-artifact.js');
  const good = { id: 1, name: 'AnotherEden_test_request-1.apks', expired: false, size_in_bytes: 123 };
  assert.equal(readyApksArtifact({ artifacts: [good] }, 'request-1'), good);
  for (const wrong of [{ ...good, expired: true }, { ...good, size_in_bytes: 0 },
    { ...good, name: 'AnotherEden_test_request-2.apks' }, { ...good, name: 'private_request-1.apks' }])
    assert.equal(readyApksArtifact({ artifacts: [wrong] }, 'request-1'), null);
});
