const { config, allrelicModuleConfig, resolveModuleCommit } = require("../../_shared/github.js");
const { allrelicConfig } = require("../_config.js");

module.exports = async function handler(req, res) {
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end("Method Not Allowed");
    return;
  }
  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");
  res.setHeader("vercel-cdn-cache-control", "public, s-maxage=60, stale-while-revalidate=300");
  try {
    const game = allrelicConfig();
    const commit = await resolveModuleCommit(allrelicModuleConfig(config()), "main", {
      preferPrebuilt: true,
      requireAsset: game.moduleAsset
    });
    if (!commit.prebuilt) {
      res.statusCode = 503;
      res.end(JSON.stringify({ message: "The precompiled ARKit release module is not ready yet" }));
      return;
    }
    res.statusCode = 200;
    res.end(JSON.stringify({ ...commit, architecture: game.architecture, asset: game.moduleAsset }));
  } catch (_) {
    res.statusCode = 502;
    res.end(JSON.stringify({ message: "Could not resolve the latest precompiled ARKit module" }));
  }
};
