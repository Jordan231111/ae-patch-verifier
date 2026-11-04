const test = require('node:test');
const assert = require('node:assert/strict');

class Context {
  constructor() {
    this.values = new Map(); this.alarmAt = null; this.alarmWrites = 0; this.lock = Promise.resolve();
    this.storage = {
      get: async key => structuredClone(this.values.get(key)),
      put: async (key, value) => this.values.set(key, structuredClone(value)),
      getAlarm: async () => this.alarmAt,
      setAlarm: async value => { this.alarmAt = value; this.alarmWrites++; }
    };
  }
  blockConcurrencyWhile(callback) {
    const result = this.lock.then(callback); this.lock = result.catch(() => {}); return result;
  }
}

test('one persistent alarm survives concurrent wakes and object restarts', async () => {
  const { ReleaseTimer } = await import('../workers/relevel-timer/src/index.mjs');
  const ctx = new Context();
  const timer = new ReleaseTimer(ctx, {});
  await Promise.all(Array.from({ length: 30 }, () => timer.fetch(new Request('https://timer/wake', { method: 'POST' }))));
  const original = ctx.alarmAt;
  assert.equal(ctx.alarmWrites, 1);
  await new ReleaseTimer(ctx, {}).fetch(new Request('https://timer/wake', { method: 'POST' }));
  assert.equal(ctx.alarmAt, original);
  assert.equal(ctx.alarmWrites, 1);
  ctx.alarmAt = null;
  await timer.fetch(new Request('https://timer/wake', { method: 'POST' }));
  assert.equal(ctx.alarmAt, original);
});

test('duplicate alarms issue one check and schedule the next without visitors', async t => {
  const { ReleaseTimer } = await import('../workers/relevel-timer/src/index.mjs');
  let now = 1000000, calls = 0;
  t.mock.method(Date, 'now', () => now);
  t.mock.method(globalThis, 'fetch', async () => {
    calls++;
    return Response.json({ status: 'checking', checkedAt: '2026-11-04T12:00:00Z' });
  });
  const ctx = new Context(), timer = new ReleaseTimer(ctx, { CHECK_URL: 'https://site/api/relevel/tick' });
  await timer.fetch(new Request('https://timer/wake', { method: 'POST' }));
  now = ctx.alarmAt;
  await Promise.all([timer.alarm(), timer.alarm()]);
  assert.equal(calls, 1);
  assert.equal(ctx.alarmAt, now + 15 * 60 * 1000);
  now = ctx.alarmAt;
  await new ReleaseTimer(ctx, { CHECK_URL: 'https://site/api/relevel/tick' }).alarm();
  assert.equal(calls, 2);
  assert.equal(ctx.values.get('schedule').failures, 0);
});

test('network failures retain a recovery alarm and retry without visitors', async t => {
  const { ReleaseTimer } = await import('../workers/relevel-timer/src/index.mjs');
  let now = 1000000;
  t.mock.method(Date, 'now', () => now);
  const network = t.mock.method(globalThis, 'fetch', async () => { throw new Error('offline'); });
  const ctx = new Context(), timer = new ReleaseTimer(ctx, { CHECK_URL: 'https://site/api/relevel/tick' });
  await timer.alarm();
  assert.equal(ctx.alarmAt, now + 60000);
  assert.equal(ctx.values.get('schedule').failures, 1);
  assert.match(ctx.values.get('schedule').lastError, /offline/);
  network.mock.mockImplementation(async () => Response.json({ status: 'ready' }));
  now = ctx.alarmAt;
  await timer.alarm();
  assert.equal(ctx.values.get('schedule').failures, 0);
  assert.equal(ctx.alarmAt, now + 15 * 60 * 1000);
});

test('health reads do not start timers and arbitrary paths cannot create scheduler identities', async () => {
  const { default: worker, ReleaseTimer } = await import('../workers/relevel-timer/src/index.mjs');
  const ctx = new Context();
  await new ReleaseTimer(ctx, {}).fetch(new Request('https://timer/health'));
  assert.equal(ctx.alarmAt, null);
  assert.equal((await worker.fetch(new Request('https://timer/other'), {})).status, 404);
  const ids = [];
  const env = { RELEASE_TIMER: { idFromName(name) { ids.push(name); return name; }, get() { return { fetch: async () => new Response('ok') }; } } };
  await worker.fetch(new Request('https://timer/wake?key=one'), env);
  await worker.fetch(new Request('https://timer/wake?key=two'), env);
  assert.deepEqual(ids, ['relevel-release-check', 'relevel-release-check']);
});

test('a persisted daily cap prevents network requests and resets on the next UTC day', async t => {
  const { ReleaseTimer } = await import('../workers/relevel-timer/src/index.mjs');
  let now = 1000000, calls = 0;
  t.mock.method(Date, 'now', () => now);
  t.mock.method(globalThis, 'fetch', async () => { calls++; return Response.json({ status: 'ready' }); });
  const ctx = new Context();
  ctx.values.set('schedule', { nextCheckAt: now, tickDay: 0, ticks: 200, failures: 0 });
  const timer = new ReleaseTimer(ctx, { CHECK_URL: 'https://site/api/relevel/tick' });
  await timer.alarm();
  assert.equal(calls, 0);
  assert.equal(ctx.alarmAt, 24 * 60 * 60 * 1000);
  assert.equal(ctx.values.get('schedule').dailyLimitReached, true);
  now = ctx.alarmAt;
  await timer.alarm();
  assert.equal(calls, 1);
  assert.equal(ctx.values.get('schedule').ticks, 1);
  assert.equal(ctx.values.get('schedule').dailyLimitReached, undefined);
});
