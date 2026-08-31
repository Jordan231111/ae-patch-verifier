const { onceworldConfig } = require("./_config.js");
const { resolvePreferredVersion } = require("./_version-source.js");

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
    const latest = await resolvePreferredVersion(game);
    res.statusCode = 200;
    res.end(JSON.stringify({
      ...latest,
      architecture: game.architecture
    }));
  } catch (error) {
    res.statusCode = 502;
    res.end(JSON.stringify({ message: error.message }));
  }
};
