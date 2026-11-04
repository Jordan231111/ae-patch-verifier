const { relevelConfig } = require('./_config.js');
const { resolveReady } = require('./_ready.js');
const { needsTimerCheck, requestDirectRefresh } = require('./_refresh.js');

module.exports = async function handler(req, res) {
  res.setHeader('content-type', 'application/json');
  res.setHeader('cache-control', 'no-store');
  if (req.method !== 'POST') {
    res.statusCode = 405; res.setHeader('allow', 'POST'); res.end('Method Not Allowed'); return;
  }
  try {
    const cfg = relevelConfig();
    const { ready, state } = await resolveReady(cfg);
    const refresh = needsTimerCheck(state) ? await requestDirectRefresh(cfg) : { requested: false };
    const status = ready ? 'ready' : state?.state === 'ready' ? 'checking' : state?.state || 'checking';
    res.statusCode = 200;
    res.end(JSON.stringify({ status, checkedAt: state?.checkedAt || null, ...refresh }));
  } catch (_) {
    res.statusCode = 503;
    res.end(JSON.stringify({ status: 'unavailable', message: 'The background release check could not start.' }));
  }
};
