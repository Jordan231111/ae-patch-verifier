const { config, githubJson, githubRequest } = require('../_shared/github.js');
const { readyApksAsset } = require('../_shared/release.js');
const { findBuildRun, findBuildArtifact } = require('../_shared/build-artifact.js');

module.exports = async function handler(req, res) {
  res.setHeader('cache-control', 'no-store');
  if (req.method !== 'GET') {
    res.statusCode = 405; res.setHeader('allow', 'GET'); res.end('Method Not Allowed'); return;
  }
  try {
    const url = new URL(req.url || '/', 'http://localhost');
    const nonce = url.searchParams.get('nonce') || '';
    if (!/^[A-Za-z0-9._-]{1,128}$/.test(nonce)) {
      res.statusCode = 400; res.end(JSON.stringify({ message: 'Missing or invalid nonce' })); return;
    }
    const cfg = config();
    if (cfg.builderMode !== 'github') {
      res.statusCode = 409; res.end(JSON.stringify({ message: 'GitHub builder is required' })); return;
    }
    const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
    const run = await findBuildRun(cfg, nonce, Number(url.searchParams.get('runId')));
    const artifact = await findBuildArtifact(cfg, nonce, run);
    let apiPath, name;
    if (artifact) {
      // GitHub retains /zip as the API route for archive:false artifacts; the
      // signed storage response is the original .apks, with no outer ZIP.
      apiPath = `${base}/actions/artifacts/${artifact.id}/zip`; name = artifact.name;
    } else {
      let release;
      try { release = await githubJson(cfg, 'GET', `${base}/releases/tags/lspatch-${encodeURIComponent(nonce)}`); }
      catch (error) { if (error.status !== 404) throw error; }
      const asset = readyApksAsset(release);
      if (!asset) { res.statusCode = 404; res.end(JSON.stringify({ message: 'Build is not ready for download' })); return; }
      apiPath = `${base}/releases/assets/${asset.id}`; name = asset.name;
    }
    const signed = await githubRequest(cfg, { method: 'GET', apiPath, accept: 'application/octet-stream' });
    const location = signed.headers?.location;
    if (!location) { res.statusCode = 502; res.end(JSON.stringify({ message: 'GitHub did not return a download URL' })); return; }
    res.statusCode = 302; res.setHeader('location', location); res.setHeader('x-asset-name', name); res.end();
  } catch (error) {
    res.statusCode = 502; res.end(JSON.stringify({ message: error.message }));
  }
};
