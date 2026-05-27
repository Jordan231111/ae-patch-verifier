const { config, githubJson, githubRequest } = require("../_shared/github.js");

function parseNonce(value) {
  if (!value || typeof value !== "string") return "";
  if (!/^[A-Za-z0-9._-]{1,128}$/.test(value)) return "";
  return value;
}

module.exports = async function handler(req, res) {
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end("Method Not Allowed");
    return;
  }

  try {
    const requestUrl = new URL(req.url || "/", "http://localhost");
    const nonce = parseNonce(requestUrl.searchParams.get("nonce"));
    if (!nonce) {
      res.statusCode = 400;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ message: "Missing or invalid nonce" }));
      return;
    }

    const cfg = config();
    if (cfg.builderMode !== "github") {
      res.statusCode = 409;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ message: "Download endpoint is only available in github builder mode" }));
      return;
    }

    const ownerRepo = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
    let release;
    try {
      release = await githubJson(cfg, "GET", `${ownerRepo}/releases/tags/lspatch-${encodeURIComponent(nonce)}`);
    } catch (error) {
      const status = error.status === 404 ? 404 : 502;
      res.statusCode = status;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ message: status === 404 ? "Build not ready yet" : error.message }));
      return;
    }

    const assets = Array.isArray(release.assets) ? release.assets : [];
    const asset = assets.find(item => item.name && item.name.endsWith(".apks"));
    if (!asset) {
      res.statusCode = 404;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ message: "Release has no .apks asset yet" }));
      return;
    }

    const apiPath = `${ownerRepo}/releases/assets/${asset.id}`;
    const signed = await githubRequest(cfg, {
      method: "GET",
      apiPath,
      accept: "application/octet-stream"
    });
    const location = signed.headers && signed.headers.location;
    if (!location) {
      res.statusCode = 502;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({ message: "GitHub did not return a signed download URL" }));
      return;
    }

    res.statusCode = 302;
    res.setHeader("location", location);
    res.setHeader("cache-control", "no-store");
    res.setHeader("x-asset-name", asset.name);
    res.end();
  } catch (error) {
    res.statusCode = 502;
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify({ message: error.message }));
  }
};
