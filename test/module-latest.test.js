const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { EventEmitter } = require('node:events');

function load(file, dependencies) {
  const module = { exports: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', file), 'utf8'),
    { module, require: id => dependencies[id], URL, process, Buffer });
  return module.exports;
}
function response() {
  return { headers: {}, setHeader(k, v) { this.headers[k] = v; }, end(body) { this.body = body; } };
}

test('latest module metadata requires the chosen prebuilt flavor and exposes the full commit', async () => {
  const calls = [];
  const helper = {
    config: () => ({ moduleOwner: 'owner', moduleRepo: 'module' }),
    normalizeModuleSource: s => s === 'houdini-x64-rewrite' ? s : 'main',
    moduleSourceRef: (_, s) => s,
    resolveModuleCommit: async (_, source, options) => {
      calls.push({ source, asset: options.requireAsset });
      return { sha: 'a'.repeat(40), shortSha: 'a'.repeat(7), prebuilt: true };
    }
  };
  const handler = load('api/module/latest.js', { '../_shared/github.js': helper });
  for (const [variant, asset] of [['release', 'app-release.apk'], ['debug', 'app-debug.apk'], ['invalid', 'app-release.apk']]) {
    const res = response();
    await handler({ method: 'GET', url: '/api/module/latest?variant=' + variant }, res);
    assert.equal(res.statusCode, 200);
    const body = JSON.parse(res.body);
    assert.equal(body.sha, 'a'.repeat(40)); assert.equal(body.asset, asset); assert.equal(body.prebuilt, true);
    assert.equal(calls.at(-1).asset, asset);
  }
  const res = response();
  await handler({ method: 'POST' }, res);
  assert.equal(res.statusCode, 405);
});

test('a debug-only release cannot be presented as the latest release APK', async () => {
  const head = 'a'.repeat(40), previous = 'b'.repeat(40);
  const responses = {
    '/repos/owner/module/commits?sha=main&per_page=30': [{ sha: head }, { sha: previous }],
    '/repos/owner/module/releases?per_page=100': [
      { tag_name: 'module-' + head, assets: [{ name: 'app-debug.apk' }] },
      { tag_name: 'module-' + previous, assets: [{ name: 'app-release.apk' }, { name: 'app-debug.apk' }] }
    ]
  };
  const https = { request(options, callback) {
    const req = new EventEmitter();
    req.setTimeout = () => {}; req.write = () => {};
    req.end = () => queueMicrotask(() => {
      const res = new EventEmitter(); res.statusCode = 200; res.headers = {}; res.setEncoding = () => {};
      callback(res); res.emit('data', JSON.stringify(responses[options.path])); res.emit('end');
    });
    return req;
  } };
  const api = load('api/_shared/github.js', { https });
  const cfg = { githubToken: 'test-token', moduleOwner: 'owner', moduleRepo: 'module', moduleRef: 'main' };
  assert.equal((await api.resolveModuleCommit(cfg, 'main', { requireAsset: 'app-release.apk' })).sha, previous);
  assert.equal((await api.resolveModuleCommit(cfg, 'main', { requireAsset: 'app-debug.apk' })).sha, head);
});
