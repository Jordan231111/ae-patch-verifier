// Multiple game routes share one deployment entry point on Vercel Hobby.
// Static imports keep bundling explicit; app handlers retain their method checks.
const handlers = Object.freeze({
  "allrelic/version": require("../server/allrelic/version.js"),
  "allrelic/module/latest": require("../server/allrelic/module/latest.js"),
  "allrelic/build": require("../server/allrelic/build.js"),
  "allrelic/status": require("../server/allrelic/status.js"),
  "allrelic/download": require("../server/allrelic/download.js"),
  "onceworld/version": require("../server/onceworld/version.js"),
  "onceworld/module/latest": require("../server/onceworld/module/latest.js"),
  "onceworld/build": require("../server/onceworld/build.js"),
  "onceworld/status": require("../server/onceworld/status.js"),
  "onceworld/download": require("../server/onceworld/download.js")
});

module.exports = async function handler(req, res) {
  const url = new URL(req.url || "/", "http://localhost");
  const original = /^\/api\/(allrelic|onceworld)\/([a-z]+(?:\/[a-z]+)?)$/.exec(url.pathname);
  const game = original ? original[1] : req.query?.game ?? url.searchParams.get("game");
  const action = original ? original[2] : req.query?.action ?? url.searchParams.get("action");
  const key = typeof game === "string" && typeof action === "string" ? `${game}/${action}` : "";
  const route = Object.hasOwn(handlers, key) ? handlers[key] : null;
  if (!route) {
    res.statusCode = 404;
    res.setHeader("content-type", "application/json");
    res.setHeader("cache-control", "no-store");
    res.end(JSON.stringify({ message: "Unknown workshop route" }));
    return;
  }
  return route(req, res);
};
