const INTERVAL_MS = 15 * 60 * 1000;
const RETRY_MS = 60 * 1000;
const SINGLETON = 'relevel-release-check';
const DAY_MS = 24 * 60 * 60 * 1000;
const MAX_DAILY_CHECKS = 200;

export class ReleaseTimer {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  async fetch(request) {
    const path = new URL(request.url).pathname;
    if (request.method === 'GET' && path === '/health') {
      const schedule = await this.ctx.storage.get('schedule');
      return Response.json({ ...schedule, alarmAt: await this.ctx.storage.getAlarm() });
    }
    if (request.method !== 'POST' || path !== '/wake') return new Response('Not found', { status: 404 });
    // Every cold start, deployment and caller addresses the same object. Repeated
    // wakes restore a missing alarm without adding timers or postponing its due time.
    return this.ctx.blockConcurrencyWhile(async () => {
      let schedule = await this.ctx.storage.get('schedule');
      if (!schedule) {
        schedule = { nextCheckAt: Date.now() + 1000, failures: 0 };
        await this.ctx.storage.put('schedule', schedule);
      }
      const alarm = await this.ctx.storage.getAlarm();
      if (alarm === null) await this.ctx.storage.setAlarm(Math.max(Date.now() + 1, schedule.nextCheckAt));
      return Response.json({ scheduled: true, nextCheckAt: schedule.nextCheckAt });
    });
  }

  async alarm() {
    const startedAt = Date.now();
    const claimed = await this.ctx.blockConcurrencyWhile(async () => {
      const schedule = await this.ctx.storage.get('schedule') || { nextCheckAt: startedAt, failures: 0 };
      if (schedule.nextCheckAt > startedAt) {
        await this.ctx.storage.setAlarm(schedule.nextCheckAt);
        return null;
      }
      const tickDay = Math.floor(startedAt / DAY_MS);
      const ticks = schedule.tickDay === tickDay ? schedule.ticks : 0;
      if (ticks >= MAX_DAILY_CHECKS) {
        const paused = { ...schedule, nextCheckAt: (tickDay + 1) * DAY_MS, dailyLimitReached: true };
        await this.ctx.storage.put('schedule', paused);
        await this.ctx.storage.setAlarm(paused.nextCheckAt);
        return null;
      }
      // Persist a recovery wake before network I/O. A crash cannot leave the
      // singleton asleep, and at-least-once alarm delivery cannot overlap checks.
      const recovery = { ...schedule, nextCheckAt: startedAt + RETRY_MS, tickDay, ticks: ticks + 1 };
      delete recovery.dailyLimitReached;
      await this.ctx.storage.put('schedule', recovery);
      await this.ctx.storage.setAlarm(recovery.nextCheckAt);
      return recovery;
    });
    if (!claimed) return;
    let result;
    try {
      const target = new URL(this.env.CHECK_URL);
      if (target.protocol !== 'https:') throw new Error('HTTPS check endpoint required');
      const response = await fetch(target, { method: 'POST', signal: AbortSignal.timeout(25000) });
      if (!response.ok) throw new Error(`Release check returned HTTP ${response.status}`);
      const body = await response.json();
      if (!['ready', 'checking', 'preparing', 'retry_pending', 'requires_review'].includes(body.status)) {
        throw new Error('Release check returned an invalid status');
      }
      result = { nextCheckAt: startedAt + INTERVAL_MS, lastTickAt: Date.now(),
        checkedAt: body.checkedAt || null, lastStatus: body.status, failures: 0,
        tickDay: claimed.tickDay, ticks: claimed.ticks };
    } catch (error) {
      // Cloudflare's built-in retries are finite; keep a durable recovery alarm
      // even through an outage lasting longer than that retry window.
      const delay = Math.min(INTERVAL_MS, RETRY_MS * 2 ** Math.min(claimed.failures, 4));
      result = { ...claimed, nextCheckAt: Date.now() + delay,
        failures: claimed.failures + 1, lastError: String(error.message).slice(0, 300) };
    }
    await this.ctx.blockConcurrencyWhile(async () => {
      await this.ctx.storage.put('schedule', result);
      await this.ctx.storage.setAlarm(result.nextCheckAt);
    });
  }
}

export default {
  fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (!['/wake', '/health'].includes(path)) return new Response('Not found', { status: 404 });
    // Callers cannot create additional scheduler identities.
    return env.RELEASE_TIMER.get(env.RELEASE_TIMER.idFromName(SINGLETON)).fetch(request);
  }
};
