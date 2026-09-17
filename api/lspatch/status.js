const { config, githubJson } = require('../_shared/github.js');
const { readyApksAsset } = require('../_shared/release.js');
const { findBuildRun, findBuildArtifact } = require('../_shared/build-artifact.js');

module.exports = async function handler(req, res) {
  res.setHeader('content-type', 'application/json');
  res.setHeader('cache-control', 'no-store');
  if (req.method !== 'GET') {
    res.statusCode = 405; res.setHeader('allow', 'GET'); res.end('Method Not Allowed'); return;
  }
  try {
    const url = new URL(req.url || '/', 'http://localhost');
    const nonce = url.searchParams.get('nonce') || '';
    if (!/^[A-Za-z0-9._-]{1,128}$/.test(nonce)) {
      res.statusCode = 400; res.end(JSON.stringify({ status: 'error', message: 'Missing or invalid nonce' })); return;
    }
    const cfg = config();
    if (cfg.builderMode !== 'github') {
      res.statusCode = 409; res.end(JSON.stringify({ status: 'error', message: 'GitHub builder is required' })); return;
    }
    const run = await findBuildRun(cfg, nonce, Number(url.searchParams.get('runId')));
    if (!run) {
      res.statusCode = 200; res.end(JSON.stringify({ status: 'queued', message: 'Waiting for GitHub Actions to register the run' })); return;
    }
    const artifact = await findBuildArtifact(cfg, nonce, run);
    if (artifact) {
      res.statusCode = 200;
      res.end(JSON.stringify({ status: 'ready', runId: run.id, filename: artifact.name,
        sizeBytes: artifact.size_in_bytes, downloadUrl: `/api/lspatch/download?nonce=${encodeURIComponent(nonce)}&runId=${run.id}` }));
      return;
    }
    // Preserve downloads from builds started before the artifact migration.
    if (run.status === 'completed' && run.conclusion === 'success') {
      let release;
      try { release = await githubJson(cfg, 'GET', `/repos/${cfg.githubOwner}/${cfg.githubRepo}/releases/tags/lspatch-${encodeURIComponent(nonce)}`); }
      catch (error) { if (error.status !== 404) throw error; }
      const asset = readyApksAsset(release);
      if (asset) {
        res.statusCode = 200; res.end(JSON.stringify({ status: 'ready', runId: run.id,
          filename: asset.name, sizeBytes: asset.size, downloadUrl: `/api/lspatch/download?nonce=${encodeURIComponent(nonce)}&runId=${run.id}` })); return;
      }
    }
    res.statusCode = 200;
    res.end(JSON.stringify(run.status === 'completed'
      ? { status: 'failed', conclusion: run.conclusion === 'success' ? 'artifact expired or unavailable' : run.conclusion, runId: run.id, runUrl: run.html_url || '' }
      : { status: 'running', runStatus: run.status, runId: run.id, runUrl: run.html_url || '', startedAt: run.run_started_at || run.created_at || '' }));
  } catch (error) {
    res.statusCode = 502; res.end(JSON.stringify({ status: 'error', message: error.message }));
  }
};
