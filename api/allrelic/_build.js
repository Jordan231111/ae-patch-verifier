const { githubJson } = require("../_shared/github.js");
const { readyApksAsset } = require("../_shared/release.js");

function parseNonce(value) {
  return typeof value === "string" && /^[0-9]{13}-[0-9a-f]{8}$/.test(value) ? value : "";
}

async function findRelease(cfg, nonce) {
  try {
    const release = await githubJson(cfg, "GET",
      `/repos/${cfg.githubOwner}/${cfg.githubRepo}/releases/tags/allrelic-lspatch-${nonce}`);
    const asset = readyApksAsset(release);
    return asset && asset.name.startsWith("AllRelic_") && asset.name.endsWith(`_${nonce}.apks`)
      && Number.isSafeInteger(asset.id) && asset.id > 0 ? asset : null;
  } catch (error) {
    if (error.status === 404) return null;
    throw error;
  }
}

async function findRun(cfg, nonce) {
  const data = await githubJson(cfg, "GET",
    `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/workflows/${encodeURIComponent(cfg.allrelicWorkflow)}/runs?event=workflow_dispatch&branch=${encodeURIComponent(cfg.githubRef)}&per_page=100`);
  const workflow = `.github/workflows/${cfg.allrelicWorkflow}`;
  return (Array.isArray(data.workflow_runs) ? data.workflow_runs : []).find(run =>
    (run.display_title || "").endsWith(` ${nonce}`) && run.event === "workflow_dispatch"
    && run.head_branch === cfg.githubRef && String(run.path || "").split("@")[0] === workflow) || null;
}

module.exports = { parseNonce, findRelease, findRun };
