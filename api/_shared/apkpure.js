const https = require("https");

const RETRY_DELAYS_MS = [0, 250, 750, 1500];

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function latestUrl(packageName) {
  return `https://d.apkpure.net/b/XAPK/${encodeURIComponent(packageName)}?version=latest`;
}

function requestLatestRedirect(packageName) {
  const url = latestUrl(packageName);
  return new Promise((resolve, reject) => {
    const request = https.get(url, {
      headers: {
        "user-agent": "Mozilla/5.0 LSPatch Workshop",
        accept: "*/*"
      }
    }, response => {
      response.resume();
      const location = response.headers.location || "";
      if (response.statusCode >= 300 && response.statusCode < 400 && location) {
        resolve(new URL(location, url));
        return;
      }
      reject(new Error(`APKPure latest lookup returned HTTP ${response.statusCode || 0}`));
    });
    request.setTimeout(15000, () => request.destroy(new Error("APKPure latest lookup timed out")));
    request.on("error", reject);
  });
}

function decodeRedirectToken(location, packageName) {
  const token = location.pathname.split("/").filter(Boolean).pop() || "";
  const normalized = token.replace(/-/g, "+").replace(/_/g, "/");
  const padded = normalized + "=".repeat((4 - (normalized.length % 4)) % 4);
  const decoded = Buffer.from(padded, "base64").toString("utf8");
  const prefix = `${packageName}_`;
  if (!decoded.startsWith(prefix)) {
    throw new Error("APKPure redirect token does not match the requested package");
  }
  const match = /^(.*)_(\d+)_([^_]*)$/.exec(decoded);
  if (!match || match[1] !== packageName) {
    throw new Error("Could not parse APKPure versionCode from the latest redirect");
  }
  return { token, decoded, versionCode: match[2] };
}

function versionNameFromLocation(location) {
  const filename = location.searchParams.get("filename") || "";
  // APKPure's CDN filename ends in _<version>_APKPure.xapk. Keep the parser
  // independent of the app display name, which may itself contain underscores.
  const match = /_([^_]+)_APKPure\.xapk$/i.exec(filename);
  return match ? match[1] : "";
}

async function resolveLatestXapk(packageName) {
  if (!/^[A-Za-z0-9._-]+$/.test(packageName || "")) {
    throw new Error("Invalid APK package name");
  }

  let lastError;
  for (const delay of RETRY_DELAYS_MS) {
    if (delay) await sleep(delay);
    try {
      const location = await requestLatestRedirect(packageName);
      const parsed = decodeRedirectToken(location, packageName);
      return {
        packageName,
        versionCode: parsed.versionCode,
        versionName: versionNameFromLocation(location)
      };
    } catch (error) {
      lastError = error;
    }
  }
  throw new Error(`Could not resolve the latest APKPure version after ${RETRY_DELAYS_MS.length} attempts: ${lastError ? lastError.message : "unknown error"}`);
}

module.exports = {
  resolveLatestXapk
};
