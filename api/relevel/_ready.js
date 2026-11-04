const { githubJson, resolveModuleCommit } = require('../_shared/github.js');
const { resolveLatestPlayListing } = require('../_shared/googleplay.js');

const SHA = /^[a-f0-9]{64}$/;
const COMMIT = /^[a-f0-9]{40}$/;
const positive = n => Number.isSafeInteger(n) && n > 0;

function validatedReady(state, cfg, module, listing) {
  const ready = state?.ready;
  const checkedAt = Date.parse(state?.checkedAt);
  const age = Date.now() - checkedAt;
  if (!Number.isFinite(age) || age < -300000 || age > 60 * 60 * 1000) return null;
  if (state?.schema !== 1 || state.packageName !== cfg.packageName || state.architecture !== cfg.architecture
      || state.state !== 'ready' || !ready || ready.validated !== true
      || !COMMIT.test(ready.moduleSha || '') || ready.moduleSha !== module.sha
      || !positive(ready.versionCode) || typeof ready.versionName !== 'string'
      || ready.versionName !== state.observed?.versionName || ready.versionCode !== state.observed?.versionCode
      || state.observed?.moduleSha !== ready.moduleSha
      || (listing && listing.versionName !== ready.versionName)
      || ready.certificateSha256 !== cfg.certificateSha256
      || !SHA.test(ready.bundleSha256 || '') || !SHA.test(ready.cacheKey || '')
      || !positive(ready.sizeBytes) || !positive(ready.releaseAssetId)
      || !/^ReLevel[-_][A-Za-z0-9._+-]+\.apks$/.test(ready.filename || '')) return null;
  return ready;
}

async function resolveReady(cfg) {
  const [module, release, listing] = await Promise.all([
    resolveModuleCommit(cfg, 'main', { requireAsset: 'app-release.apk' }),
    githubJson(cfg, 'GET', `/repos/${cfg.githubOwner}/${cfg.githubRepo}/releases/tags/${encodeURIComponent(cfg.readyTag)}`)
      .catch(error => { if (error.status === 404) return null; throw error; }),
    resolveLatestPlayListing(cfg.packageName, { attempts: 1, timeoutMs: 3000 }).catch(() => null)
  ]);
  let state = null;
  if (release && !release.draft && typeof release.body === 'string' && release.body.length < 65536) {
    try { state = JSON.parse(release.body); } catch (_) { /* An incomplete index cannot be ready. */ }
  }
  const ready = module.prebuilt ? validatedReady(state, cfg, module, listing) : null;
  return { ready, module, state, listing };
}
module.exports = { resolveReady, validatedReady };
