const https = require("https");

const RETRY_DELAYS_MS = [0, 300, 900];

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function requestListing(packageName, country, language) {
  const query = new URLSearchParams({ id: packageName, gl: country, hl: language });
  return new Promise((resolve, reject) => {
    const request = https.get(`https://play.google.com/store/apps/details?${query}`, {
      headers: { "user-agent": "Mozilla/5.0 LSPatch Workshop", accept: "text/html" }
    }, response => {
      let html = "";
      response.setEncoding("utf8");
      response.on("data", chunk => { html += chunk; });
      response.on("end", () => {
        if (response.statusCode !== 200) {
          reject(new Error(`Google Play listing returned HTTP ${response.statusCode || 0}`));
          return;
        }
        resolve(html);
      });
    });
    request.setTimeout(15000, () => request.destroy(new Error("Google Play listing timed out")));
    request.on("error", reject);
  });
}

function listingVersion(html) {
  const scripts = html.match(/>AF_initDataCallback[\s\S]*?<\/script/g) || [];
  for (const script of scripts) {
    if (!/key: 'ds:5'/.test(script)) continue;
    const match = /data:([\s\S]*?), sideChannel: {}}\);<\//.exec(script);
    if (!match) continue;
    const root = JSON.parse(match[1])?.[1]?.[2];
    const fallback = Array.isArray(root) ? root[root.length - 1]?.["141"] : null;
    const versionName = root?.[140]?.[0]?.[0]?.[0] || fallback?.[0]?.[0]?.[0] || "";
    const updatedSeconds = root?.[145]?.[0]?.[1]?.[0] || fallback?.[5]?.[0]?.[1]?.[0];
    return {
      versionName: typeof versionName === "string" ? versionName.trim() : "",
      updatedAt: Number.isSafeInteger(updatedSeconds) ? updatedSeconds * 1000 : null
    };
  }
  throw new Error("Google Play listing data was not found");
}

async function resolveLatestPlayListing(packageName, options = {}) {
  if (!/^[A-Za-z0-9._-]+$/.test(packageName || "")) {
    throw new Error("Invalid Google Play package name");
  }

  const country = /^[A-Za-z]{2}$/.test(options.country || "")
    ? options.country.toLowerCase()
    : "us";
  const language = /^[A-Za-z]{2}$/.test(options.language || "")
    ? options.language.toLowerCase()
    : "en";

  let lastError;
  for (const delay of RETRY_DELAYS_MS) {
    if (delay) await sleep(delay);
    try {
      const listing = listingVersion(await requestListing(packageName, country, language));
      const versionName = listing.versionName;
      if (!versionName || /^varies with device$/i.test(versionName)) {
        throw new Error("Google Play did not expose a concrete version name");
      }
      return {
        packageName,
        versionCode: null,
        versionName,
        source: "google-play",
        updatedAt: listing.updatedAt
      };
    } catch (error) {
      lastError = error;
    }
  }

  throw new Error(
    `Could not resolve the latest Google Play version after ${RETRY_DELAYS_MS.length} attempts: ${lastError ? lastError.message : "unknown error"}`
  );
}

module.exports = { resolveLatestPlayListing };
