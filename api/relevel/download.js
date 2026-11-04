const { relevelConfig } = require('./_config.js');
const { resolveReady } = require('./_ready.js');
const { githubRequest } = require('../_shared/github.js');

module.exports = async function handler(req, res) {
  res.setHeader('cache-control', 'no-store');
  if (req.method !== 'GET') {
    res.statusCode = 405; res.setHeader('allow', 'GET'); res.end('Method Not Allowed'); return;
  }
  try {
    const url = new URL(req.url || '/', 'http://localhost');
    const id = url.searchParams.get('id') || '';
    if (!/^[a-f0-9]{64}$/.test(id)) { res.statusCode = 400; res.end('Invalid download identity'); return; }
    const cfg = relevelConfig();
    const { ready } = await resolveReady(cfg);
    if (!ready || ready.cacheKey !== id) {
      res.statusCode = 409; res.setHeader('content-type', 'application/json');
      res.end(JSON.stringify({ message: 'The verified release changed or is still being prepared. Refresh and retry.' })); return;
    }
    const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
    const artifactReady = Number.isSafeInteger(ready.artifactId) && ready.artifactId > 0
      && Number.isFinite(Date.parse(ready.artifactExpiresAt)) && Date.parse(ready.artifactExpiresAt) > Date.now() + 60000;
    let result;
    if (artifactReady) {
      result = await githubRequest(cfg, { method: 'GET', apiPath: `${base}/actions/artifacts/${ready.artifactId}/zip`, accept: 'application/vnd.github+json', headersOnly: true });
    }
    if (!result || [404, 410].includes(result.status)) {
      result = await githubRequest(cfg, { method: 'GET', apiPath: `${base}/releases/assets/${ready.releaseAssetId}`, accept: 'application/octet-stream', headersOnly: true });
    }
    const location = result.headers?.location;
    if (result.status < 300 || result.status >= 400 || !location || new URL(location).protocol !== 'https:') {
      res.statusCode = 502; res.end('Download negotiation failed. Please retry.'); return;
    }
    if (url.searchParams.get('format') === 'json') {
      res.statusCode = 200; res.setHeader('content-type', 'application/json');
      res.end(JSON.stringify({ url: location, filename: ready.filename })); return;
    }
    res.statusCode = 302; res.setHeader('location', location); res.setHeader('x-asset-name', ready.filename); res.end();
  } catch (_) { res.statusCode = 502; res.end('The verified download is temporarily unavailable.'); }
};
