const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function handler({ due = true, ready = null, state = { state: 'ready' }, dispatch = async () => ({ requested: true }) } = {}) {
  const mod = { exports: {} }, dependencies = {
    './_config.js': { relevelConfig: () => ({}) },
    './_ready.js': { resolveReady: async () => ({ ready, state }) },
    './_refresh.js': { needsRefresh: () => due, requestDirectRefresh: dispatch }
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../api/relevel/tick.js'), 'utf8'), {
    module: mod, require: id => { assert.ok(dependencies[id], id); return dependencies[id]; }, JSON
  });
  return mod.exports;
}
function response() { return { headers: {}, setHeader(k, v) { this.headers[k] = v; }, end(body) { this.body = body; } }; }

test('an overdue timer tick dispatches directly without recursively waking itself', async () => {
  const res = response(); await handler()({ method: 'POST' }, res);
  assert.equal(res.statusCode, 200);
  assert.deepEqual(JSON.parse(res.body), { status: 'checking', checkedAt: null, requested: true });
});
test('fresh ticks do not dispatch work', async () => {
  const res = response();
  await handler({ due: false, ready: {}, dispatch: async () => { throw new Error('unexpected dispatch'); } })({ method: 'POST' }, res);
  assert.equal(res.statusCode, 200);
  assert.equal(JSON.parse(res.body).requested, false);
});
test('dispatch failures cause retryable failures and GET cannot run a check', async () => {
  let calls = 0;
  const run = handler({ dispatch: async () => { calls++; throw new Error('offline'); } });
  const get = response(); await run({ method: 'GET' }, get);
  assert.equal(get.statusCode, 405); assert.equal(calls, 0);
  const post = response(); await run({ method: 'POST' }, post);
  assert.equal(post.statusCode, 503); assert.equal(calls, 1);
  assert.equal(JSON.parse(post.body).status, 'unavailable');
});
