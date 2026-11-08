const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const sha = 'a'.repeat(40);
const nonce = '1794110400000-1234abcd';
const cfg = { builderMode: 'github', githubOwner: 'owner', githubRepo: 'builder',
  githubRef: 'main', allrelicWorkflow: 'build-allrelic-apks.yml' };
function load(file, dependencies) {
  const module = { exports: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', file), 'utf8'),
    { module, require: id => dependencies[id], URL, process, Buffer });
  return module.exports;
}
function response() {
  return { headers: {}, setHeader(k, v) { this.headers[k] = v; }, end(body) { this.body = body; } };
}
function builder({ prebuilt = true, latest = {}, fail } = {}) {
  const calls = [];
  const route = load('api/allrelic/build.js', {
    crypto: { randomBytes: () => Buffer.from('1234abcd', 'hex') },
    '../_shared/github.js': {
      config: () => cfg, allrelicModuleConfig: value => value,
      resolveModuleCommit: async (_, source, options) => {
        assert.equal(options.requireAsset, 'app-release.apk');
        assert.equal(options.preferPrebuilt, true);
        return { sha, shortSha: sha.slice(0, 7), prebuilt };
      },
      githubJson: async (...args) => calls.push(args)
    },
    './_config.js': { allrelicConfig: () => ({ packageName: 'com.AllRelic', architecture: 'arm64-v8a', moduleAsset: 'app-release.apk' }) },
    './_version-source.js': { resolveVersion: async (_, options) => {
      assert.equal(options.preflight, true);
      if (fail) throw fail;
      return { packageName: 'com.AllRelic', versionCode: '148', versionName: '1.2.4', source: 'apkpure', ...latest };
    } }
  });
  return { route, calls };
}

test('AllRelic dispatch pins APKPure version and a completed release prebuilt', async () => {
  const { route, calls } = builder();
  const res = response();
  await route({ method: 'POST', body: { versionCode: '148', versionName: '1.2.4', packageName: 'wrong' } }, res);
  assert.equal(res.statusCode, 202);
  assert.equal(calls.length, 1);
  assert.match(calls[0][2], /build-allrelic-apks.yml\/dispatches$/);
  assert.equal(calls[0][3].inputs.moduleSha, sha);
  assert.equal(calls[0][3].inputs.versionCode, '148');
  assert.equal(calls[0][3].inputs.packageName, undefined);
  const body = JSON.parse(res.body);
  assert.match(body.statusUrl, /^\/api\/allrelic\/status\?nonce=/);
  assert.equal(body.source, 'apkpure');
});

test('a changed versionCode with the same name cannot build stale AllRelic inputs', async () => {
  const { route, calls } = builder({ latest: { versionCode: '149' } });
  const res = response();
  await route({ method: 'POST', body: { versionCode: '148', versionName: '1.2.4' } }, res);
  assert.equal(res.statusCode, 409);
  assert.equal(JSON.parse(res.body).latest.versionCode, '149');
  assert.equal(calls.length, 0);
});

test('unavailable prebuilt or ARM64 variant cannot dispatch an AllRelic build', async () => {
  for (const options of [{ prebuilt: false }, { fail: Object.assign(new Error('ARM64 not published'), { statusCode: 503 }) }]) {
    const { route, calls } = builder(options), res = response();
    await route({ method: 'POST', body: { versionCode: '148', versionName: '1.2.4' } }, res);
    assert.equal(res.statusCode, 503); assert.equal(calls.length, 0);
  }
});

test('AllRelic malformed requests are rejected before network dispatch', async () => {
  const { route, calls } = builder();
  for (const body of ['{', 'null', [], {}, { versionCode: '0', versionName: '' }]) {
    const res = response(); await route({ method: 'POST', body }, res);
    assert.equal(res.statusCode, 400);
  }
  const res = response(); await route({ method: 'GET' }, res);
  assert.equal(res.statusCode, 405); assert.equal(calls.length, 0);
});

test('AllRelic version resolver uses APKPure only and preflights the requested ABI', async () => {
  const calls = [];
  const { resolveVersion } = load('api/allrelic/_version-source.js', {
    '../_shared/apkpure.js': {
      resolveLatestXapk: async packageName => ({ packageName, versionCode: '148', versionName: '1.2.4' }),
      resolveXapkVariant: async (...args) => calls.push(args)
    }
  });
  const game = { packageName: 'com.AllRelic', architecture: 'arm64-v8a' };
  const result = await resolveVersion(game, { preflight: true });
  assert.equal(result.source, 'apkpure');
  assert.deepEqual(calls[0], ['com.AllRelic', '148', 'arm64-v8a', '1.2.4']);
});

test('draft, partial and cross-request AllRelic assets are never ready', async () => {
  const asset = { id: 123, name: `AllRelic_1.2.4_${nonce}.apks`, size: 123, state: 'uploaded' };
  let release = { draft: false, assets: [asset] };
  const api = load('api/allrelic/_build.js', {
    '../_shared/github.js': { githubJson: async () => release },
    '../_shared/release.js': require('../api/_shared/release.js')
  });
  assert.equal((await api.findRelease(cfg, nonce)).id, 123);
  for (const wrong of [{ ...release, draft: true }, { draft: false, assets: [{ ...asset, size: 0 }] },
    { draft: false, assets: [{ ...asset, state: 'starter' }] },
    { draft: false, assets: [{ ...asset, name: `OnceWorld_${nonce}.apks` }] },
    { draft: false, assets: [{ ...asset, name: 'AllRelic_wrong.apks' }] }]) {
    release = wrong;
    assert.equal(await api.findRelease(cfg, nonce), null);
  }
  assert.equal(api.parseNonce(nonce), nonce);
  assert.equal(api.parseNonce('../other'), '');
});

test('AllRelic workflow matching rejects another branch, workflow or partial nonce', async () => {
  const good = { display_title: 'AllRelic LSPatch 1.2.4 ' + nonce, event: 'workflow_dispatch',
    head_branch: 'main', path: '.github/workflows/build-allrelic-apks.yml', id: 123 };
  const api = load('api/allrelic/_build.js', {
    '../_shared/github.js': { githubJson: async () => ({ workflow_runs: [
      { ...good, id: 1, head_branch: 'other' }, { ...good, id: 2, path: 'other.yml' },
      { ...good, id: 3, display_title: good.display_title + 'extra' }, good
    ] }) }, '../_shared/release.js': {}
  });
  assert.equal((await api.findRun(cfg, nonce)).id, 123);
});

test('AllRelic negotiates storage without reading binary APK bytes', async () => {
  let location = 'https://release-assets.githubusercontent.com/signed.apks', status = 302;
  const route = load('api/allrelic/download.js', {
    '../_shared/github.js': { config: () => cfg, githubRequest: async (_, request) => {
      assert.equal(request.headersOnly, true);
      return { status, headers: { location } };
    } },
    './_build.js': { parseNonce: value => value === nonce ? nonce : '',
      findRelease: async () => ({ id: 123, name: 'AllRelic.apks' }) }
  });
  const res = response();
  await route({ method: 'GET', url: '/?nonce=' + nonce + '&format=json' }, res);
  assert.equal(res.statusCode, 200); assert.equal(JSON.parse(res.body).url, location);
  for (const wrong of [{ location: 'http://insecure.test/', status: 302 }, { location, status: 200 }]) {
    ({ location, status } = wrong);
    const rejected = response();
    await route({ method: 'GET', url: '/?nonce=' + nonce }, rejected);
    assert.equal(rejected.statusCode, 502);
  }
});

test('completed AllRelic runs without a download cannot poll as running forever', async () => {
  const route = load('api/allrelic/status.js', {
    '../_shared/github.js': { config: () => cfg },
    './_build.js': { parseNonce: () => nonce, findRelease: async () => null,
      findRun: async () => ({ status: 'completed', conclusion: 'success' }) }
  });
  const res = response(); await route({ method: 'GET', url: '/?nonce=' + nonce }, res);
  assert.equal(JSON.parse(res.body).status, 'failed');
  assert.match(JSON.parse(res.body).message, /expired/);
});
