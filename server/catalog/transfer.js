const { githubJson, githubRequest } = require("../../api/_shared/github");

// At most one short-lived redirect per catalog edition. Signed URLs stay in memory.
const redirects = new Map();

function transferRecord(variant, key, ticket, now = Date.now()) {
  const record = ticket?.transfer;
  if (!record || ticket.key !== key || record.requestKey !== key || record.variant !== variant.id
      || record.version !== ticket.version || record.signer !== variant.signer
      || !Number.isSafeInteger(record.versionCode) || record.versionCode < variant.versionCode
      || !Number.isSafeInteger(record.sizeBytes) || record.sizeBytes <= 0
      || !Number.isSafeInteger(record.artifactId) || record.artifactId <= 0
      || !Number.isSafeInteger(record.runId) || record.runId <= 0 || record.runId !== ticket.runId
      || !/^[a-f0-9]{64}$/.test(record.sha256) || !(Date.parse(record.expiresAt) > now + 5000)) return null;
  return record;
}

function cachedRedirect(variant, key, sha256) {
  const cached = redirects.get(variant.id);
  return cached?.key === key && cached.sha256 === sha256 && cached.until > Date.now() ? cached.url : null;
}

async function transferRedirect(cfg, variant, record) {
  const cached = cachedRedirect(variant, record.requestKey, record.sha256);
  if (cached) return cached;
  const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  try {
    const path = `${base}/actions/artifacts/${record.artifactId}`;
    const [asset, result] = await Promise.all([
      githubJson(cfg, "GET", path),
      githubRequest(cfg, { method: "GET", headersOnly: true, apiPath: `${path}/zip`, accept: "application/vnd.github+json" })
    ]);
    if (asset.id !== record.artifactId || asset.workflow_run?.id !== record.runId || asset.expired !== false
        || asset.name !== `${variant.id}-${record.version}.apks` || asset.size_in_bytes !== record.sizeBytes
        || asset.digest !== `sha256:${record.sha256}` || !(Date.parse(asset.expires_at) > Date.now() + 5000)) return null;
    if (result.status < 300 || result.status >= 400 || !result.headers.location) return null;
    const url = new URL(result.headers.location);
    if (url.protocol !== "https:" || url.username || url.password) return null;
    redirects.set(variant.id, { key: record.requestKey, sha256: record.sha256, url: url.href,
      until: Math.min(Date.now() + 10000, Date.parse(asset.expires_at) - 5000, Date.parse(record.expiresAt) - 5000) });
    return url.href;
  } catch {
    // A missing or expired artifact falls back to the permanent release publisher.
    return null;
  }
}

module.exports = { transferRecord, cachedRedirect, transferRedirect };
