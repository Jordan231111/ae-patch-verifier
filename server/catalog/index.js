const catalog = require("../../catalog.json");
const repository = "Jordan231111/ae-patch-verifier";
const origin = `https://github.com/${repository}/releases/download/`;

function mergeReady(seed, records) {
  return { apps: seed.apps.map(app => ({
    ...app,
    variants: app.variants.map(variant => {
      const next = records?.[variant.id];
      if (app.statusUrl || !next || typeof next.version !== "string"
          || !/^[\w.-]+$/.test(next.version) || !/^[a-f0-9]{64}$/.test(next.sha256)
          || next.signer !== variant.signer || !Number.isSafeInteger(next.versionCode)
          || next.versionCode < variant.versionCode || !Number.isSafeInteger(next.sizeBytes) || next.sizeBytes < 1
          || !Number.isFinite(Date.parse(next.updatedAt)) || Date.parse(next.updatedAt) < Date.parse(variant.updatedAt)) {
        return variant;
      }
      const expected = `${origin}catalog-${variant.id}-${next.sha256.slice(0, 20)}/${variant.id}-${next.version}.apks`;
      if (next.downloadUrl !== expected) return variant;
      const { version, versionCode, sizeBytes, sha256, updatedAt, downloadUrl, moduleVersion, moduleSha } = next;
      return { ...variant, version, versionCode, sizeBytes, sha256, updatedAt, downloadUrl,
        ...(moduleVersion && { moduleVersion }), ...(moduleSha && { moduleSha }) };
    })
  })) };
}

async function handler(req, res) {
  if (!["GET", "HEAD"].includes(req.method)) {
    res.setHeader("allow", "GET, HEAD");
    res.statusCode = 405;
    return res.end();
  }
  let records;
  try {
    const response = await fetch(`https://raw.githubusercontent.com/${repository}/workshop-ready/ready.json`,
      { signal: AbortSignal.timeout(2500) });
    if (response.ok) records = await response.json();
  } catch {
    // The deploy snapshot contains real, persistent releases and survives an origin outage.
  }
  res.setHeader("content-type", "application/json; charset=utf-8");
  res.setHeader("cache-control", "public, max-age=30");
  res.setHeader("vercel-cdn-cache-control", "public, s-maxage=60, stale-while-revalidate=300");
  res.statusCode = 200;
  res.end(req.method === "HEAD" ? undefined : JSON.stringify(mergeReady(catalog, records)));
}

module.exports = handler;
module.exports.mergeReady = mergeReady;
