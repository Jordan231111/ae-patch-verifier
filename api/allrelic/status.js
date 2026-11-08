const { config } = require("../_shared/github.js");
const { parseNonce, findRelease, findRun } = require("./_build.js");

module.exports = async function handler(req, res) {
  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end(JSON.stringify({ message: "Method Not Allowed" }));
    return;
  }
  const nonce = parseNonce(new URL(req.url || "/", "http://localhost").searchParams.get("nonce"));
  if (!nonce) {
    res.statusCode = 400;
    res.end(JSON.stringify({ message: "Missing or invalid nonce" }));
    return;
  }
  try {
    const cfg = config();
    const asset = await findRelease(cfg, nonce);
    if (asset) {
      res.statusCode = 200;
      res.end(JSON.stringify({ status: "ready", filename: asset.name, sizeBytes: asset.size,
        downloadUrl: `/api/allrelic/download?nonce=${nonce}` }));
      return;
    }
    const run = await findRun(cfg, nonce);
    res.statusCode = 200;
    if (!run) {
      res.end(JSON.stringify({ status: "queued", message: "Waiting for the AllRelic build runner" }));
    } else if (run.status === "completed") {
      res.end(JSON.stringify({ status: "failed", conclusion: run.conclusion,
        message: run.conclusion === "success" ? "The AllRelic download has expired; build again" : "AllRelic packaging failed",
        runUrl: run.html_url }));
    } else {
      res.end(JSON.stringify({ status: run.status === "queued" ? "queued" : "running", runUrl: run.html_url }));
    }
  } catch (error) {
    res.statusCode = 502;
    res.end(JSON.stringify({ status: "error", message: error.message }));
  }
};
