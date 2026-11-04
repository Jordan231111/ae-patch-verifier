const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { needsRefresh, needsTimerCheck } = require('../api/relevel/_refresh.js');
const { validatedReady, hasFreshCheck } = require('../api/relevel/_ready.js');

const cfg = { githubOwner: 'owner', githubRepo: 'builder', githubRef: 'main',
  relevelWorkflow: 'relevel-ready.yml', packageName: 'work.ponix.relevel', architecture: 'arm64-v8a',
  certificateSha256: 'c'.repeat(64) };

function readyState(age = 0) {
  return { schema: 1, packageName: cfg.packageName, architecture: cfg.architecture, state: 'ready',
    checkedAt: new Date(Date.now() - age).toISOString(),
    observed: { versionCode: 50, versionName: '1.1.6', moduleSha: 'a'.repeat(40) },
    ready: { validated: true, versionCode: 50, versionName: '1.1.6', moduleSha: 'a'.repeat(40),
      certificateSha256: cfg.certificateSha256, bundleSha256: 'b'.repeat(64), cacheKey: 'd'.repeat(64),
      filename: 'ReLevel-1.1.6-arm64-lspatched.apks', sizeBytes: 100, releaseAssetId: 123 } };
}

function loadRefresh(githubJson, fetch) {
  const mod = { exports: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../api/relevel/_refresh.js'), 'utf8'), {
    module: mod, require: id => { assert.equal(id, '../_shared/github.js'); return { githubJson }; },
    Date, Number, Map, Error, encodeURIComponent, URL, AbortSignal, fetch
  });
  return mod.exports;
}

test('missed polls are refreshed before expiration without relaxing download freshness', () => {
  assert.equal(needsRefresh(readyState()), false);
  const overdue = readyState(16 * 60 * 1000);
  assert.equal(needsRefresh(overdue), true);
  assert.ok(validatedReady(overdue, cfg));
  const stale = readyState(4 * 60 * 60 * 1000);
  assert.equal(needsRefresh(stale), true);
  assert.equal(validatedReady(stale, cfg), null);
  assert.equal(needsRefresh(null), true);
  assert.equal(needsRefresh({ checkedAt: new Date(Date.now() + 3600000).toISOString() }), true);
});

test('browser requests share a wake of the one durable timer instead of dispatching competing jobs', async () => {
  let wakes = 0;
  const refresh = loadRefresh(async () => { throw new Error('GitHub must be reached by the timer tick'); }, async (url, options) => {
    wakes++;
    assert.equal(url.href, 'https://timer.workers.dev/wake');
    assert.equal(options.method, 'POST');
    return Response.json({ scheduled: true, nextCheckAt: Date.now() + 1000 });
  });
  const timerCfg = { ...cfg, relevelTimerUrl: 'https://timer.workers.dev' };
  await Promise.all(Array.from({ length: 30 }, () => refresh.requestRefresh(timerCfg)));
  assert.equal(wakes, 1);
});

test('retry deadlines are respected and become eligible without another cron event', () => {
  const state = readyState(4 * 60 * 60 * 1000);
  state.state = 'retry_pending';
  state.retryAt = new Date(Date.now() + 60000).toISOString();
  assert.equal(needsRefresh(state), false);
  state.checkedAt = new Date().toISOString();
  state.retryAt = new Date(Date.now() - 1).toISOString();
  assert.equal(needsRefresh(state), true);
});

test('CI completion latency does not turn the 15-minute timer into a 30-minute poll', () => {
  const afterCi = readyState(14 * 60 * 1000);
  assert.equal(needsRefresh(afterCi), false);
  assert.equal(needsTimerCheck(afterCi), true);
  assert.equal(needsTimerCheck(readyState(2 * 60 * 1000)), false);
  afterCi.state = 'retry_pending';
  afterCi.retryAt = new Date(Date.now() + 60000).toISOString();
  assert.equal(needsTimerCheck(afterCi), false);
});

test('concurrent status requests share one public workflow dispatch and never force builds', async () => {
  const calls = [];
  const refresh = loadRefresh(async (_, method, apiPath, body) => {
    calls.push({ method, apiPath, body });
    return method === 'GET' ? { workflow_runs: [] } : {};
  });
  await Promise.all(Array.from({ length: 20 }, () => refresh.requestRefresh(cfg)));
  await refresh.requestRefresh(cfg);
  assert.equal(calls.length, 2);
  assert.match(calls[0].apiPath, /\/actions\/workflows\/relevel-ready.yml\/runs\?/);
  assert.equal(calls[1].apiPath, '/repos/owner/builder/actions/workflows/relevel-ready.yml/dispatches');
  assert.deepEqual(JSON.parse(JSON.stringify(calls[1].body)), { ref: 'main', inputs: { force: false } });
});

test('recent workflow history prevents another dispatch after a serverless cold start', async () => {
  for (const status of ['queued', 'in_progress', 'completed']) {
    const calls = [];
    const refresh = loadRefresh(async (_, method) => {
      calls.push(method);
      return { workflow_runs: [{ head_branch: 'main', created_at: new Date().toISOString(), status }] };
    });
    await refresh.requestRefresh(cfg);
    assert.deepEqual(calls, ['GET']);
  }
});

test('ancient queued runs do not prevent recovery indefinitely', async () => {
  const calls = [];
  const refresh = loadRefresh(async (_, method) => {
    calls.push(method);
    return { workflow_runs: [{ head_branch: 'main', status: 'queued',
      created_at: new Date(Date.now() - 4 * 60 * 60 * 1000).toISOString() }] };
  });
  await refresh.requestRefresh(cfg);
  assert.deepEqual(calls, ['GET', 'POST']);
});

test('failed dispatches are reported and throttled instead of starting a request storm', async () => {
  let calls = 0;
  const refresh = loadRefresh(async (_, method) => {
    calls++;
    if (method === 'POST') throw new Error('permission denied');
    return { workflow_runs: [] };
  });
  await assert.rejects(refresh.requestRefresh(cfg), /permission denied/);
  await assert.rejects(refresh.requestRefresh(cfg), /permission denied/);
  assert.equal(calls, 2);
});

function statusHandler(state, requestRefresh) {
  const mod = { exports: {} };
  const dependencies = {
    './_config.js': { relevelConfig: () => cfg },
    './_ready.js': { resolveReady: async () => ({ state, ready: validatedReady(state, cfg), module: { sha: state?.observed?.moduleSha } }), hasFreshCheck },
    './_refresh.js': { needsRefresh, requestRefresh }
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../api/relevel/status.js'), 'utf8'), {
    module: mod, require: id => { assert.ok(dependencies[id], id); return dependencies[id]; },
    JSON, console: { error() {} }
  });
  return mod.exports;
}
function response() { return { setHeader() {}, end(body) { this.body = JSON.parse(body); } }; }

test('stale ready indexes request a refresh and report checking rather than a nonexistent build', async () => {
  let requested = 0;
  const run = statusHandler(readyState(4 * 60 * 60 * 1000), async () => { requested++; });
  const res = response();
  await run({ method: 'GET' }, res);
  assert.equal(requested, 1);
  assert.equal(res.statusCode, 200);
  assert.equal(res.body.status, 'checking');
  assert.doesNotMatch(res.body.message, /being prepared/);
  assert.equal(res.body.downloadUrl, undefined);
});

test('refresh failures cannot claim a build is running or enable an expired download', async () => {
  const run = statusHandler(readyState(4 * 60 * 60 * 1000), async () => { throw new Error('denied'); });
  const res = response();
  await run({ method: 'GET' }, res);
  assert.equal(res.statusCode, 503);
  assert.equal(res.body.status, 'unavailable');
  assert.equal(res.body.downloadUrl, undefined);
});

test('a still-fresh verified release remains downloadable if its early refresh fails', async () => {
  const run = statusHandler(readyState(16 * 60 * 1000), async () => { throw new Error('denied'); });
  const res = response();
  await run({ method: 'GET' }, res);
  assert.equal(res.statusCode, 200);
  assert.equal(res.body.status, 'ready');
});

test('actual preparations, retry backoff and compatibility review keep their distinct states', async () => {
  for (const status of ['preparing', 'retry_pending', 'requires_review']) {
    const state = readyState(); state.state = status;
    state.retryAt = new Date(Date.now() + 60000).toISOString();
    const run = statusHandler(state, async () => { throw new Error('unexpected refresh'); });
    const res = response(); await run({ method: 'GET' }, res);
    assert.equal(res.body.status, status);
    assert.equal(res.body.downloadUrl, undefined);
  }
});

test('the visible release panel continues polling after it becomes ready', async () => {
  const source = fs.readFileSync(path.join(__dirname, '../index.html'), 'utf8');
  const code = source.slice(source.indexOf('    async function refreshRelevel()'), source.indexOf('    async function downloadRelevel()'));
  for (const status of ['ready', 'checking']) {
    const timers = [];
    const node = () => ({ classList: { contains: () => false } });
    const context = { relevelRefreshSequence: 0, relevelRefreshTimer: null, relevelDownloading: false,
      latestRelevel: null, relevelPanel: node(), relevelVersion: node(), relevelVersionMeta: node(),
      relevelModule: node(), relevelModuleMeta: node(), relevelButton: node(), document: { hidden: false },
      window: { clearTimeout() {}, setTimeout(_, delay) { timers.push(delay); } },
      fetch: async () => ({ ok: true }), readJsonSafe: async () => ({ status, versionName: '1.1.6', sizeBytes: 100, moduleSha: 'a'.repeat(40) }),
      setRelevelStatus() {}, Math, Error };
    vm.runInNewContext(code, context);
    await context.refreshRelevel();
    assert.deepEqual(timers, [status === 'ready' ? 60000 : 15000]);
  }
});

test('an expired download immediately resumes readiness checks', async () => {
  const source = fs.readFileSync(path.join(__dirname, '../index.html'), 'utf8');
  const code = source.slice(source.indexOf('    async function downloadRelevel()'), source.indexOf('    function setOnceworldStatus('));
  let refreshed = 0;
  const context = { latestRelevel: readyState().ready, relevelDownloading: false, relevelButton: {},
    setRelevelStatus() {}, sameOriginApiUrl: () => '/api/relevel/download',
    fetch: async () => ({ ok: false }), readJsonSafe: async () => ({ message: 'Expired release check' }),
    refreshRelevel: async () => {
      refreshed++;
      assert.equal(context.latestRelevel, null);
      assert.equal(context.relevelDownloading, false);
    }, encodeURIComponent, Error, URL };
  vm.runInNewContext(code, context);
  await context.downloadRelevel();
  assert.equal(refreshed, 1);
  assert.equal(context.relevelButton.disabled, true);
});
