const { config, githubJson } = require("../_shared/github.js");

function parseNonce(value) {
  if (!value || typeof value !== "string") return "";
  if (!/^[A-Za-z0-9._-]{1,128}$/.test(value)) return "";
  return value;
}

async function findRun(cfg, nonce) {
  const ownerRepo = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  const runsPath = `${ownerRepo}/actions/workflows/${encodeURIComponent(cfg.githubWorkflow)}/runs?event=workflow_dispatch&branch=${encodeURIComponent(cfg.githubRef)}&per_page=30`;
  const data = await githubJson(cfg, "GET", runsPath);
  const runs = Array.isArray(data.workflow_runs) ? data.workflow_runs : [];
  return runs.find(candidate => (candidate.display_title || candidate.name || "").includes(nonce)) || null;
}

async function findRelease(cfg, nonce) {
  const ownerRepo = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  try {
    return await githubJson(cfg, "GET", `${ownerRepo}/releases/tags/lspatch-${encodeURIComponent(nonce)}`);
  } catch (error) {
    if (error.status === 404) return null;
    throw error;
  }
}

module.exports = async function handler(req, res) {
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end("Method Not Allowed");
    return;
  }

  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");

  try {
    const requestUrl = new URL(req.url || "/", "http://localhost");
    const nonce = parseNonce(requestUrl.searchParams.get("nonce"));
    if (!nonce) {
      res.statusCode = 400;
      res.end(JSON.stringify({ status: "error", message: "Missing or invalid nonce" }));
      return;
    }

    const cfg = config();
    if (cfg.builderMode !== "github") {
      res.statusCode = 200;
      res.end(JSON.stringify({ status: "ready", message: "Local builder returns the file directly from /api/lspatch/build" }));
      return;
    }

    const release = await findRelease(cfg, nonce);
    if (release && Array.isArray(release.assets) && release.assets.length) {
      const asset = release.assets.find(item => item.name && item.name.endsWith(".apks"));
      if (asset) {
        res.statusCode = 200;
        res.end(JSON.stringify({
          status: "ready",
          filename: asset.name,
          sizeBytes: asset.size || 0,
          downloadUrl: `/api/lspatch/download?nonce=${encodeURIComponent(nonce)}`
        }));
        return;
      }
    }

    const run = await findRun(cfg, nonce);
    if (!run) {
      res.statusCode = 200;
      res.end(JSON.stringify({ status: "queued", message: "Waiting for GitHub Actions to register the run" }));
      return;
    }

    if (run.status === "completed") {
      if (run.conclusion === "success") {
        res.statusCode = 200;
        res.end(JSON.stringify({
          status: "ready",
          downloadUrl: `/api/lspatch/download?nonce=${encodeURIComponent(nonce)}`
        }));
        return;
      }
      res.statusCode = 200;
      res.end(JSON.stringify({
        status: "failed",
        conclusion: run.conclusion || "unknown",
        runUrl: run.html_url || ""
      }));
      return;
    }

    res.statusCode = 200;
    res.end(JSON.stringify({
      status: "running",
      runStatus: run.status,
      runUrl: run.html_url || "",
      startedAt: run.run_started_at || run.created_at || ""
    }));
  } catch (error) {
    res.statusCode = 502;
    res.end(JSON.stringify({ status: "error", message: error.message }));
  }
};
