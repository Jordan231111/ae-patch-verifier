const https = require("https");

const RETRY_DELAYS_MS = [0, 250, 750, 1500];
const TRUSTED_DOWNLOAD_HOSTS = ["apkpure.net", "winudf.com"];

class ApkPureVariantUnavailableError extends Error {
  constructor({ packageName, versionCode, versionName, architecture, cause }) {
    const version = versionName
      ? `${versionName} (versionCode ${versionCode})`
      : `versionCode ${versionCode}`;
    super(
      `APKPure has not published an XAPK for ${packageName} ${version} with ${architecture} yet` +
      (cause ? `: ${cause.message}` : "")
    );
    this.name = "ApkPureVariantUnavailableError";
    this.code = "APKPURE_VARIANT_UNAVAILABLE";
    this.statusCode = 503;
    if (cause) this.cause = cause;
  }
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function latestUrl(packageName) {
  return `https://d.apkpure.net/b/XAPK/${encodeURIComponent(packageName)}?version=latest`;
}

function variantUrl(packageName, versionCode, architecture) {
  const params = new URLSearchParams({
    versionCode: String(versionCode),
    nc: architecture
  });
  return `https://d.apkpure.net/b/XAPK/${encodeURIComponent(packageName)}?${params}`;
}

function requestRedirect(url) {
  return new Promise((resolve, reject) => {
    const request = https.get(url, {
      headers: {
        "user-agent": "Mozilla/5.0 LSPatch Workshop",
        accept: "*/*"
      }
    }, response => {
      const location = response.headers.location || "";
      if (response.statusCode >= 300 && response.statusCode < 400 && location) {
        response.resume();
        resolve(new URL(location, url));
        return;
      }
      // This endpoint should only redirect. Do not accidentally drain a full
      // XAPK into memory/network if APKPure changes it to return HTTP 200.
      response.destroy();
      reject(new Error(`APKPure lookup returned HTTP ${response.statusCode || 0}`));
    });
    request.setTimeout(15000, () => request.destroy(new Error("APKPure lookup timed out")));
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

function isTrustedDownloadLocation(location) {
  const hostname = location.hostname.toLowerCase();
  return location.protocol === "https:" && TRUSTED_DOWNLOAD_HOSTS.some(domain =>
    hostname === domain || hostname.endsWith(`.${domain}`)
  );
}

function parseXapkRedirect(location, packageName, expectedVersionCode = "") {
  if (/\/versions\/?$/.test(location.pathname)) {
    const error = new Error("APKPure redirected to its versions page instead of an XAPK");
    error.code = "APKPURE_VARIANT_UNAVAILABLE";
    throw error;
  }
  if (!isTrustedDownloadLocation(location)) {
    throw new Error(`APKPure redirected to an unexpected host: ${location.hostname || "<missing>"}`);
  }

  const parsed = decodeRedirectToken(location, packageName);
  if (expectedVersionCode && parsed.versionCode !== String(expectedVersionCode)) {
    throw new Error(
      `APKPure returned versionCode ${parsed.versionCode} instead of ${expectedVersionCode}`
    );
  }

  const sizeText = location.searchParams.get("full_size") || "";
  const fullSize = Number(sizeText);
  if (!/^[1-9][0-9]*$/.test(sizeText) || !Number.isSafeInteger(fullSize)) {
    throw new Error("APKPure redirect did not include a valid download size");
  }

  return {
    packageName,
    versionCode: parsed.versionCode,
    versionName: versionNameFromLocation(location),
    downloadUrl: location.toString(),
    fullSize
  };
}

async function resolveRedirect(url, packageName, expectedVersionCode = "") {
  let lastError;
  for (const delay of RETRY_DELAYS_MS) {
    if (delay) await sleep(delay);
    try {
      const location = await requestRedirect(url);
      return parseXapkRedirect(location, packageName, expectedVersionCode);
    } catch (error) {
      lastError = error;
    }
  }
  throw lastError || new Error("APKPure lookup failed");
}

async function resolveLatestXapk(packageName) {
  if (!/^[A-Za-z0-9._-]+$/.test(packageName || "")) {
    throw new Error("Invalid APK package name");
  }

  try {
    const resolved = await resolveRedirect(latestUrl(packageName), packageName);
    return {
      packageName: resolved.packageName,
      versionCode: resolved.versionCode,
      versionName: resolved.versionName
    };
  } catch (error) {
    throw new Error(
      `Could not resolve the latest APKPure version after ${RETRY_DELAYS_MS.length} attempts: ${error.message}`,
      { cause: error }
    );
  }
}

async function resolveXapkVariant(packageName, versionCode, architecture, versionName = "") {
  if (!/^[A-Za-z0-9._-]+$/.test(packageName || "")) {
    throw new Error("Invalid APK package name");
  }
  if (!/^[1-9][0-9]*$/.test(String(versionCode || ""))) {
    throw new Error("Invalid APK version code");
  }
  if (!/^[A-Za-z0-9._-]+$/.test(architecture || "")) {
    throw new Error("Invalid APK architecture");
  }

  try {
    // Variant availability is a fast preflight, not a retry loop. The actual
    // downloader has its own transfer retries and resolves a fresh signed URL.
    const location = await requestRedirect(
      variantUrl(packageName, versionCode, architecture)
    );
    const resolved = parseXapkRedirect(location, packageName, versionCode);
    if (versionName && resolved.versionName && resolved.versionName !== versionName) {
      throw new Error(
        `APKPure returned version ${resolved.versionName} instead of ${versionName}`
      );
    }
    return resolved;
  } catch (error) {
    if (error.code === "APKPURE_VARIANT_UNAVAILABLE") {
      throw new ApkPureVariantUnavailableError({
        packageName,
        versionCode,
        versionName,
        architecture,
        cause: error
      });
    }
    const lookupError = new Error(
      `Could not verify the ${architecture} APKPure XAPK for ${packageName}: ${error.message}`,
      { cause: error }
    );
    lookupError.statusCode = 502;
    throw lookupError;
  }
}

module.exports = {
  ApkPureVariantUnavailableError,
  resolveLatestXapk,
  resolveXapkVariant,
  // Pure helpers are exported for deterministic contract tests.
  parseXapkRedirect,
  variantUrl
};
