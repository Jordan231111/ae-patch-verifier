const { config, normalizeModuleSource, moduleSourceRef, resolveModuleCommit } = require("../_shared/github.js");

module.exports = async function handler(req, res) {
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end("Method Not Allowed");
    return;
  }

  const cfg = config();
  const requestUrl = new URL(req.url || "/", "http://localhost");
  const moduleSource = normalizeModuleSource(requestUrl.searchParams.get("moduleSource"));

  res.setHeader("content-type", "application/json");
  // Edge-cache for 60s and let stale results serve another 5 min while revalidating.
  // Browsers see no-store so the UI keeps the freshness story; only Vercel's edge dedupes.
  res.setHeader("cache-control", "no-store");
  res.setHeader("cdn-cache-control", "public, s-maxage=60, stale-while-revalidate=300");
  res.setHeader("vercel-cdn-cache-control", "public, s-maxage=60, stale-while-revalidate=300");

  try {
    if (!cfg.moduleOwner || !cfg.moduleRepo) {
      throw new Error("Module commit source is not configured");
    }
    const commit = await resolveModuleCommit(cfg, moduleSource);
    res.statusCode = 200;
    res.end(JSON.stringify({
      shortSha: commit.shortSha,
      moduleSource,
      ref: moduleSourceRef(cfg, moduleSource)
    }));
  } catch (error) {
    res.statusCode = 500;
    res.end(JSON.stringify({ message: "Could not resolve latest module commit" }));
  }
};
