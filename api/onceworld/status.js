const { config, githubJson } = require("../_shared/github.js");

function parseNonce(value) {
  if (!value || typeof value !== "string") return "";
  return /^[A-Za-z0-9._-]{1,128}$/.test(value) ? value : "";
}

async function findRun(cfg, nonce) {
  const ownerRepo = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  const path = `${ownerRepo}/actions/workflows/${encodeURIComponent(cfg.onceworldWorkflow)}/runs?event=workflow_dispatch&branch=${encodeURIComponent(cfg.githubRef)}&per_page=100`;
  const data = await githubJson(cfg, "GET", path);
  const runs = Array.isArray(data.workflow_runs) ? data.workflow_runs : [];
  return runs.find(run => (run.display_title || run.name || "").includes(nonce)) || null;
}

async function findRelease(cfg, nonce) {
  try {
    return await githubJson(
      cfg,
      "GET",
      `/repos/${cfg.githubOwner}/${cfg.githubRepo}/releases/tags/onceworld-lspatch-${encodeURIComponent(nonce)}`
    );
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
    const release = await findRelease(cfg, nonce);
    const asset = release && Array.isArray(release.assets)
      ? release.assets.find(item => item.name && item.name.endsWith(".apks"))
      : null;
    if (asset) {
      res.statusCode = 200;
      res.end(JSON.stringify({
        status: "ready",
        filename: asset.name,
        sizeBytes: asset.size || 0,
        downloadUrl: `/api/onceworld/download?nonce=${encodeURIComponent(nonce)}`
      }));
      return;
    }

    const run = await findRun(cfg, nonce);
    if (!run) {
      res.statusCode = 200;
      res.end(JSON.stringify({ status: "queued", message: "Waiting for GitHub Actions to register the build" }));
      return;
    }

    if (run.status === "completed") {
      if (run.conclusion === "success") {
        res.statusCode = 200;
        res.end(JSON.stringify({
          status: "running",
          runStatus: "publishing",
          runUrl: run.html_url || ""
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
