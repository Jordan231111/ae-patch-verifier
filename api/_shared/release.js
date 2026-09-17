// A listed asset can still be a partial upload. Do not advertise drafts or starters.
function readyApksAsset(release) {
  if (!release || release.draft !== false || !Array.isArray(release.assets)) return null;
  return release.assets.find(asset => asset && typeof asset.name === 'string'
    && asset.name.endsWith('.apks') && asset.state === 'uploaded'
    && Number.isSafeInteger(asset.size) && asset.size > 0) || null;
}
module.exports = { readyApksAsset };
