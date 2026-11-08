const { resolveLatestXapk, resolveXapkVariant } = require("../../api/_shared/apkpure.js");

async function resolveVersion(game, { preflight = false } = {}) {
  const latest = await resolveLatestXapk(game.packageName);
  if (preflight) {
    await resolveXapkVariant(game.packageName, latest.versionCode, game.architecture, latest.versionName);
  }
  return { ...latest, architecture: game.architecture, source: "apkpure" };
}

module.exports = { resolveVersion };
