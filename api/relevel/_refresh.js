const { githubJson } = require('../_shared/github.js');

const CHECK_INTERVAL_MS = 15 * 60 * 1000;
const DISPATCH_COOLDOWN_MS = 5 * 60 * 1000;
const requests = new Map();

function needsRefresh(state) {
  const now = Date.now();
  if (state?.state === 'retry_pending') {
    const retryAt = Date.parse(state.retryAt);
    if (Number.isFinite(retryAt)) return retryAt <= now;
  }
  const checkedAt = Date.parse(state?.checkedAt);
  return !Number.isFinite(checkedAt) || checkedAt > now + 300000 || now - checkedAt >= CHECK_INTERVAL_MS;
}

async function dispatchIfDue(cfg) {
  if (!cfg.githubOwner || !cfg.githubRepo || !cfg.githubRef || !cfg.relevelWorkflow) {
    throw new Error('ReLevel update checks are not configured');
  }
  const workflow = `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/workflows/${encodeURIComponent(cfg.relevelWorkflow)}`;
  // Run history supplies a cooldown across serverless instances. Workflow
  // concurrency serializes any simultaneous dispatches from separate cold starts.
  const history = await githubJson(cfg, 'GET', `${workflow}/runs?branch=${encodeURIComponent(cfg.githubRef)}&per_page=10`);
  if (!Array.isArray(history.workflow_runs)) throw new Error('GitHub workflow history is unavailable');
  const now = Date.now();
  const recent = history.workflow_runs.some(run => {
    const age = now - Date.parse(run.created_at);
    return run.head_branch === cfg.githubRef && Number.isFinite(age) && age >= -300000 && age < DISPATCH_COOLDOWN_MS;
  });
  if (recent) return { requested: false };
  // CI alone reads Play/private module credentials and decides whether inputs
  // changed. An unchanged release refreshes only its small verified index.
  await githubJson(cfg, 'POST', `${workflow}/dispatches`, { ref: cfg.githubRef, inputs: { force: false } });
  return { requested: true };
}

async function coalesce(key, operation) {
  const now = Date.now();
  const existing = requests.get(key);
  if (existing && existing.until > now) return existing.promise;
  const entry = { until: now + DISPATCH_COOLDOWN_MS, promise: operation() };
  requests.set(key, entry);
  try {
    return await entry.promise;
  } catch (error) {
    entry.until = Date.now() + 30000;
    throw error;
  }
}

function requestDirectRefresh(cfg) {
  const key = `${cfg.githubOwner}/${cfg.githubRepo}/${cfg.githubRef}/${cfg.relevelWorkflow}`;
  return coalesce(key, () => dispatchIfDue(cfg));
}

async function requestRefresh(cfg) {
  if (!cfg.relevelTimerUrl) return requestDirectRefresh(cfg);
  const target = new URL('/wake', cfg.relevelTimerUrl);
  if (target.protocol !== 'https:') throw new Error('HTTPS timer endpoint required');
  return coalesce(target.href, async () => {
    const response = await fetch(target, { method: 'POST', signal: AbortSignal.timeout(8000) });
    if (!response.ok) throw new Error('The background timer could not be reached');
    const body = await response.json();
    if (body.scheduled !== true || !Number.isFinite(body.nextCheckAt)) throw new Error('The background timer did not schedule a check');
    return body;
  });
}

module.exports = { needsRefresh, requestRefresh, requestDirectRefresh };
