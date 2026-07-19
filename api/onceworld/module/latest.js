const {
  config,
  onceworldModuleConfig,
  resolveModuleCommit
} = require("../../_shared/github.js");
const { onceworldConfig } = require("../_config.js");

module.exports = async function handler(req, res) {
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end("Method Not Allowed");
    return;
  }

  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");
  res.setHeader("cdn-cache-control", "public, s-maxage=60, stale-while-revalidate=300");
  res.setHeader("vercel-cdn-cache-control", "public, s-maxage=60, stale-while-revalidate=300");

  try {
    const game = onceworldConfig();
    const cfg = onceworldModuleConfig(config());
    const commit = await resolveModuleCommit(cfg, "main", {
      preferPrebuilt: true,
      requireAsset: game.moduleAsset
    });
    if (!commit.prebuilt) {
      throw new Error("No precompiled OnceWorld release module is available");
    }
    res.statusCode = 200;
    res.end(JSON.stringify({
      shortSha: commit.shortSha,
      ref: commit.ref,
      prebuilt: true,
      architecture: game.architecture
    }));
  } catch (error) {
    res.statusCode = 500;
    res.end(JSON.stringify({ message: "Could not resolve the latest precompiled OnceWorld module" }));
  }
};
