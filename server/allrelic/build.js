const crypto = require("crypto");
const { config, allrelicModuleConfig, githubJson, resolveModuleCommit } = require("../../api/_shared/github.js");
const { allrelicConfig } = require("./_config.js");
const { resolveVersion } = require("./_version-source.js");

module.exports = async function handler(req, res) {
  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");
  if (req.method !== "POST") {
    res.statusCode = 405;
    res.setHeader("allow", "POST");
    res.end(JSON.stringify({ message: "Method Not Allowed" }));
    return;
  }
  try {
    const cfg = config();
    const game = allrelicConfig();
    if (cfg.builderMode !== "github" || !cfg.githubOwner || !cfg.githubRepo) {
      res.statusCode = 409;
      res.end(JSON.stringify({ message: "AllRelic requires the configured GitHub builder" }));
      return;
    }
    let body;
    try {
      body = typeof req.body === "object" && req.body !== null ? req.body : JSON.parse(req.body || "{}");
    } catch (_) {
      res.statusCode = 400;
      res.end(JSON.stringify({ message: "Invalid JSON request" }));
      return;
    }
    if (!body || !/^[1-9][0-9]*$/.test(String(body.versionCode || ""))
        || typeof body.versionName !== "string" || body.versionName.length > 64) {
      res.statusCode = 400;
      res.end(JSON.stringify({ message: "Select the latest AllRelic version before building" }));
      return;
    }
    const [latest, moduleCommit] = await Promise.all([
      resolveVersion(game, { preflight: true }),
      resolveModuleCommit(allrelicModuleConfig(cfg), "main", {
        preferPrebuilt: true,
        requireAsset: game.moduleAsset
      })
    ]);
    if (String(body.versionCode) !== String(latest.versionCode)
        || body.versionName !== latest.versionName) {
      res.statusCode = 409;
      res.end(JSON.stringify({ message: "AllRelic updated; review the latest version and build again", latest }));
      return;
    }
    if (!moduleCommit.prebuilt) {
      res.statusCode = 503;
      res.end(JSON.stringify({ message: "The precompiled ARKit release module is not ready yet" }));
      return;
    }
    const nonce = `${Date.now()}-${crypto.randomBytes(4).toString("hex")}`;
    await githubJson(cfg, "POST",
      `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/workflows/${encodeURIComponent(cfg.allrelicWorkflow)}/dispatches`, {
        ref: cfg.githubRef,
        inputs: {
          versionCode: String(latest.versionCode),
          versionName: latest.versionName || "unknown",
          moduleSha: moduleCommit.sha,
          nonce
        }
      });
    res.statusCode = 202;
    res.end(JSON.stringify({
      mode: "github", ...latest, nonce,
      moduleSha: moduleCommit.sha, moduleShortSha: moduleCommit.shortSha,
      statusUrl: `/api/allrelic/status?nonce=${encodeURIComponent(nonce)}`,
      downloadUrl: `/api/allrelic/download?nonce=${encodeURIComponent(nonce)}`
    }));
  } catch (error) {
    res.statusCode = error.statusCode || 502;
    res.end(JSON.stringify({ message: error.message }));
  }
};
