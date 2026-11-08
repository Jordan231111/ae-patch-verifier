const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const config = require('../vercel.json');

function response() {
  return { headers: {}, setHeader(k, v) { this.headers[k] = v; }, end(body) { this.body = body; } };
}
function router() {
  const calls = [];
  const module = { exports: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../api/workshop.js'), 'utf8'), {
    module, URL, require: file => async (req, res) => {
      calls.push({ file, req }); res.statusCode = 200; res.end('ok');
    }
  });
  return { handler: module.exports, calls };
}

test('all ten public app routes retain request methods, bodies and nonce queries', async () => {
  const { handler, calls } = router();
  for (const game of ['allrelic', 'onceworld']) {
    for (const action of ['version', 'module/latest', 'build', 'status', 'download']) {
      const req = { method: action === 'build' ? 'POST' : 'GET', body: { versionCode: 148 },
        url: `/api/${game}/${action}?nonce=request-1` };
      const res = response(); await handler(req, res);
      assert.equal(calls.at(-1).file, `../server/${game}/${action}.js`);
      assert.equal(calls.at(-1).req, req);
      assert.equal(res.statusCode, 200);
    }
  }
  assert.equal(calls.length, 10);
});

test('Vercel rewritten destinations route through the same explicit handler table', async () => {
  const { handler, calls } = router();
  for (const query of [undefined, { game: 'allrelic', action: 'module/latest' }]) {
    await handler({ method: 'GET', query,
      url: '/api/workshop?game=allrelic&action=module/latest&nonce=request-1' }, response());
    assert.equal(calls.at(-1).file, '../server/allrelic/module/latest.js');
  }
  // Client query parameters cannot change a known public route's game.
  await handler({ method: 'GET', url: '/api/onceworld/version?game=allrelic&action=build' }, response());
  assert.equal(calls.at(-1).file, '../server/onceworld/version.js');
});

test('unknown, prototype and array routes never load arbitrary source files', async () => {
  const { handler, calls } = router();
  for (const query of [{ game: 'allrelic', action: '../../private' }, { game: 'toString', action: '' },
    { game: ['allrelic'], action: 'build' }, { game: 'onceworld', action: ['build'] }, {}]) {
    const res = response(); await handler({ method: 'GET', url: '/api/workshop', query }, res);
    assert.equal(res.statusCode, 404);
  }
  assert.equal(calls.length, 0);
});

test('new apps do not require extra Vercel functions or public API URL changes', () => {
  for (const game of ['allrelic', 'onceworld']) {
    for (const action of ['version', 'module/latest', 'build', 'status', 'download']) {
      assert.ok(config.rewrites.some(rule => rule.source === `/api/${game}/${action}`
        && rule.destination === `/api/workshop?game=${game}&action=${action}`));
    }
    assert.equal(fs.existsSync(path.join(__dirname, '../api', game)), false);
  }
  assert.equal(config.functions['api/workshop.js'].maxDuration, 60);
  function entrypoints(directory, prefix = 'api') {
    return fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
      if (entry.name.startsWith('_')) return [];
      const relative = prefix + '/' + entry.name;
      return entry.isDirectory() ? entrypoints(path.join(directory, entry.name), relative)
        : entry.name.endsWith('.js') ? [relative] : [];
    });
  }
  assert.deepEqual(entrypoints(path.join(__dirname, '../api')).sort(), Object.keys(config.functions).sort());
  assert.ok(Object.keys(config.functions).length <= 12);
});
