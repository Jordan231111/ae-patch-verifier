const crypto = require("crypto");
const {
  config,
  onceworldModuleConfig,
  githubJson,
  resolveModuleCommit
} = require("../_shared/github.js");
const { resolveLatestXapk } = require("../_shared/apkpure.js");
const { onceworldConfig } = require("./_config.js");

async function dispatchGithubBuild(requestedVersionCode) {
  const cfg = config();
  const game = onceworldConfig();
  if (!cfg.githubOwner || !cfg.githubRepo) {
    throw new Error("GitHub builder repository is not configured");
  }

  const moduleCfg = onceworldModuleConfig(cfg);
  const moduleCommit = await resolveModuleCommit(moduleCfg, "main", {
    preferPrebuilt: true,
    requireAsset: game.moduleAsset
  });
  if (!moduleCommit.prebuilt) {
    throw new Error("No precompiled OnceWorld release module is available yet");
  }

  const latest = await resolveLatestXapk(game.packageName);
  if (requestedVersionCode && String(requestedVersionCode) !== latest.versionCode) {
    const error = new Error(`OnceWorld updated to ${latest.versionName || `code ${latest.versionCode}`}; refresh and try again`);
    error.status = 409;
    error.latest = latest;
    throw error;
  }

  const nonce = `${Date.now()}-${crypto.randomBytes(4).toString("hex")}`;
  const versionLabel = (latest.versionName || `code${latest.versionCode}`).replace(/[^A-Za-z0-9._-]+/g, "_");
  const filename = `OnceWorld_${versionLabel}_LSPatched_OWKit_ARM64_${moduleCommit.shortSha}.apks`;

  await githubJson(
    cfg,
    "POST",
    `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/workflows/${encodeURIComponent(cfg.onceworldWorkflow)}/dispatches`,
    {
      ref: cfg.githubRef,
      inputs: {
        moduleSha: moduleCommit.sha,
        moduleRef: moduleCommit.ref,
        moduleAsset: game.moduleAsset,
        packageName: game.packageName,
        architecture: game.architecture,
        versionCode: latest.versionCode,
        versionName: latest.versionName || "unknown",
        nonce
      }
    }
  );

  return {
    nonce,
    filename,
    moduleShortSha: moduleCommit.shortSha,
    moduleRef: moduleCommit.ref,
    versionCode: latest.versionCode,
    versionName: latest.versionName,
    packageName: game.packageName,
    architecture: game.architecture
  };
}

module.exports = async function handler(req, res) {
  if (req.method !== "POST") {
    res.statusCode = 405;
    res.setHeader("allow", "POST");
    res.end("Method Not Allowed");
    return;
  }

  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");

  try {
    const cfg = config();
    if (cfg.builderMode !== "github") {
      res.statusCode = 409;
      res.end(JSON.stringify({ message: "OnceWorld builds require the GitHub builder" }));
      return;
    }

    const body = typeof req.body === "object" && req.body !== null
      ? req.body
      : JSON.parse(req.body || "{}");
    const requestedVersionCode = /^[0-9]+$/.test(String(body.versionCode || ""))
      ? String(body.versionCode)
      : "";
    const dispatched = await dispatchGithubBuild(requestedVersionCode);
    res.statusCode = 202;
    res.end(JSON.stringify({
      mode: "github",
      ...dispatched,
      statusUrl: `/api/onceworld/status?nonce=${encodeURIComponent(dispatched.nonce)}`,
      downloadUrl: `/api/onceworld/download?nonce=${encodeURIComponent(dispatched.nonce)}`
    }));
  } catch (error) {
    res.statusCode = error.status || 500;
    res.end(JSON.stringify({ message: error.message, latest: error.latest || undefined }));
  }
};
