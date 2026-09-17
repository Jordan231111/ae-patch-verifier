const { githubJson } = require('./github.js');

function readyApksArtifact(data, nonce) {
  return (Array.isArray(data?.artifacts) ? data.artifacts : []).find(asset =>
    Number.isSafeInteger(asset.id) && asset.id > 0 && asset.expired === false
    && Number.isSafeInteger(asset.size_in_bytes) && asset.size_in_bytes > 0
    && typeof asset.name === 'string' && asset.name.startsWith('AnotherEden_')
    && asset.name.endsWith(`_${nonce}.apks`)) || null;
}

async function findBuildRun(cfg, nonce, runId) {
  const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions`;
  const workflow = cfg.githubWorkflow.startsWith('.github/workflows/') ? cfg.githubWorkflow : `.github/workflows/${cfg.githubWorkflow}`;
  const matches = run => (run?.display_title || run?.name || '').endsWith(` ${nonce}`)
    && run.event === 'workflow_dispatch' && run.head_branch === cfg.githubRef
    && String(run.path || '').split('@')[0] === workflow;
  if (Number.isSafeInteger(runId) && runId > 0) {
    const run = await githubJson(cfg, 'GET', `${base}/runs/${runId}`);
    return matches(run) ? run : null;
  }
  const data = await githubJson(cfg, 'GET', `${base}/workflows/${encodeURIComponent(cfg.githubWorkflow)}/runs?event=workflow_dispatch&branch=${encodeURIComponent(cfg.githubRef)}&per_page=100`);
  return (Array.isArray(data.workflow_runs) ? data.workflow_runs : []).find(matches) || null;
}

async function findBuildArtifact(cfg, nonce, run) {
  if (!run) return null;
  const data = await githubJson(cfg, 'GET', `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/runs/${run.id}/artifacts?per_page=100`);
  return readyApksArtifact(data, nonce);
}
module.exports = { readyApksArtifact, findBuildRun, findBuildArtifact };
