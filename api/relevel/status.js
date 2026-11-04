const { relevelConfig } = require('./_config.js');
const { resolveReady } = require('./_ready.js');

module.exports = async function handler(req, res) {
  res.setHeader('content-type', 'application/json');
  res.setHeader('cache-control', 'no-store');
  res.setHeader('cdn-cache-control', 'public, s-maxage=15');
  if (req.method !== 'GET') {
    res.statusCode = 405; res.setHeader('allow', 'GET'); res.end('Method Not Allowed'); return;
  }
  try {
    const cfg = relevelConfig();
    const { ready, module, state, listing } = await resolveReady(cfg);
    res.statusCode = 200;
    if (ready) {
      res.end(JSON.stringify({ status: 'ready', packageName: cfg.packageName, architecture: cfg.architecture,
        versionName: ready.versionName, versionCode: ready.versionCode, moduleSha: ready.moduleSha,
        cacheKey: ready.cacheKey, filename: ready.filename, sizeBytes: ready.sizeBytes,
        preparedAt: ready.preparedAt, sha256: ready.bundleSha256,
        downloadUrl: `/api/relevel/download?id=${ready.cacheKey}` }));
    } else {
      res.end(JSON.stringify({ status: state?.state === 'requires_review' ? 'requires_review' : 'preparing',
        versionName: listing?.versionName || state?.observed?.versionName || null,
        moduleSha: module.sha, checkedAt: state?.checkedAt || null,
        message: state?.state === 'requires_review'
          ? 'The latest update is being checked. A verified download will appear when it is ready.'
          : 'The latest release is being prepared in the background. Please check again shortly.' }));
    }
  } catch (_) {
    res.statusCode = 503;
    res.end(JSON.stringify({ status: 'unavailable', message: 'The verified download is temporarily unavailable. Please retry.' }));
  }
};
