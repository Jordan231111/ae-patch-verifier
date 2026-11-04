const { githubJson } = require('../_shared/github.js');

const SHA = /^[a-f0-9]{64}$/;
const COMMIT = /^[a-f0-9]{40}$/;
const positive = n => Number.isSafeInteger(n) && n > 0;

function hasFreshCheck(state) {
  const checkedAt = Date.parse(state?.checkedAt);
  const age = Date.now() - checkedAt;
  return Number.isFinite(age) && age >= -300000 && age <= 60 * 60 * 1000;
}

function validatedReady(state, cfg) {
  const ready = state?.ready;
  if (!hasFreshCheck(state)) return null;
  if (state?.schema !== 1 || state.packageName !== cfg.packageName || state.architecture !== cfg.architecture
      || state.state !== 'ready' || !ready || ready.validated !== true
      || !COMMIT.test(ready.moduleSha || '')
      || !positive(ready.versionCode) || typeof ready.versionName !== 'string'
      || ready.versionName !== state.observed?.versionName || ready.versionCode !== state.observed?.versionCode
      || state.observed?.moduleSha !== ready.moduleSha
      || ready.certificateSha256 !== cfg.certificateSha256
      || !SHA.test(ready.bundleSha256 || '') || !SHA.test(ready.cacheKey || '')
      || !positive(ready.sizeBytes) || !positive(ready.releaseAssetId)
      || !/^ReLevel[-_][A-Za-z0-9._+-]+\.apks$/.test(ready.filename || '')) return null;
  return ready;
}

async function resolveReady(cfg) {
  const release = await githubJson(cfg, 'GET', `/repos/${cfg.githubOwner}/${cfg.githubRepo}/releases/tags/${encodeURIComponent(cfg.readyTag)}`)
    .catch(error => { if (error.status === 404) return null; throw error; });
  let state = null;
  if (release && !release.draft && typeof release.body === 'string' && release.body.length < 65536) {
    try { state = JSON.parse(release.body); } catch (_) { /* An incomplete index cannot be ready. */ }
  }
  // CI resolves the newest completed private prebuilt before updating this index.
  // A click consumes that fresh verified observation without private-repo lookups.
  const sha = state?.observed?.moduleSha;
  const module = { sha: COMMIT.test(sha || '') ? sha : null, prebuilt: COMMIT.test(sha || '') };
  return { ready: validatedReady(state, cfg), module, state };
}
module.exports = { resolveReady, validatedReady, hasFreshCheck };
