const { relevelConfig } = require('./_config.js');
const { resolveReady, hasFreshCheck } = require('./_ready.js');
const { needsRefresh, requestRefresh } = require('./_refresh.js');

module.exports = async function handler(req, res) {
  res.setHeader('content-type', 'application/json');
  res.setHeader('cache-control', 'no-store');
  res.setHeader('cdn-cache-control', 'public, s-maxage=15');
  if (req.method !== 'GET') {
    res.statusCode = 405; res.setHeader('allow', 'GET'); res.end('Method Not Allowed'); return;
  }
  try {
    const cfg = relevelConfig();
    const { ready, module, state } = await resolveReady(cfg);
    if (needsRefresh(state)) {
      try {
        // Await only the bounded API dispatch, never the background build.
        await requestRefresh(cfg);
      } catch (error) {
        console.error('ReLevel update-check dispatch failed', error.status || error.name);
        if (!ready) throw error;
      }
    }
    res.statusCode = 200;
    if (ready) {
      res.end(JSON.stringify({ status: 'ready', packageName: cfg.packageName, architecture: cfg.architecture,
        versionName: ready.versionName, versionCode: ready.versionCode, moduleSha: ready.moduleSha,
        cacheKey: ready.cacheKey, filename: ready.filename, sizeBytes: ready.sizeBytes,
        preparedAt: ready.preparedAt, sha256: ready.bundleSha256,
        downloadUrl: `/api/relevel/download?id=${ready.cacheKey}` }));
    } else {
      const status = state?.state === 'requires_review' ? 'requires_review'
        : state?.state === 'retry_pending' ? 'retry_pending'
        : state?.state === 'preparing' ? 'preparing'
        : !hasFreshCheck(state) ? 'checking' : 'unavailable';
      const messages = {
        requires_review: 'The latest update needs a compatibility review. Downloads will resume after it passes.',
        retry_pending: 'Release verification will retry automatically. This page will update when it passes.',
        preparing: 'The latest release is being prepared in the background. This page will update when it is ready.',
        checking: 'Checking for release updates. This page will update automatically.',
        unavailable: 'The verified release metadata is incomplete. Please retry shortly.'
      };
      res.statusCode = status === 'unavailable' ? 503 : 200;
      res.end(JSON.stringify({ status,
        versionName: state?.observed?.versionName || null,
        moduleSha: module.sha, checkedAt: state?.checkedAt || null,
        retryAt: state?.retryAt || null, message: messages[status] }));
    }
  } catch (_) {
    res.statusCode = 503;
    res.end(JSON.stringify({ status: 'unavailable', message: 'The release update check is temporarily unavailable. Please retry shortly.' }));
  }
};
