const { config, githubRequest } = require("../../api/_shared/github.js");
const { parseNonce, findRelease } = require("./_build.js");

module.exports = async function handler(req, res) {
  res.setHeader("cache-control", "no-store");
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end("Method Not Allowed");
    return;
  }
  const url = new URL(req.url || "/", "http://localhost");
  const nonce = parseNonce(url.searchParams.get("nonce"));
  try {
    if (!nonce) {
      res.statusCode = 400;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ message: "Missing or invalid nonce" }));
      return;
    }
    const cfg = config();
    const asset = await findRelease(cfg, nonce);
    if (!asset) {
      res.statusCode = 404;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ message: "Build is not ready or has expired" }));
      return;
    }
    const signed = await githubRequest(cfg, { method: "GET",
      apiPath: `/repos/${cfg.githubOwner}/${cfg.githubRepo}/releases/assets/${asset.id}`,
      accept: "application/octet-stream", headersOnly: true });
    const location = signed.headers?.location;
    if (signed.status < 300 || signed.status >= 400 || !location || new URL(location).protocol !== "https:") {
      throw new Error(`GitHub did not return a secure download redirect (HTTP ${signed.status})`);
    }
    res.setHeader("x-asset-name", asset.name);
    if (url.searchParams.get("format") === "json") {
      res.statusCode = 200;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ url: location, filename: asset.name }));
    } else {
      res.statusCode = 302;
      res.setHeader("location", location);
      res.end();
    }
  } catch (error) {
    res.statusCode = 502;
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify({ message: error.message }));
  }
};
