const { resolveLatestPlayListing } = require("../_shared/googleplay.js");
const { resolveLatestXapk } = require("../_shared/apkpure.js");

async function resolvePreferredVersion(game) {
  try {
    return await resolveLatestPlayListing(game.packageName, {
      country: game.playCountry,
      language: game.playLanguage
    });
  } catch (playError) {
    try {
      const fallback = await resolveLatestXapk(game.packageName);
      return {
        ...fallback,
        source: "apkpure",
        sourceWarning: playError.message
      };
    } catch (fallbackError) {
      throw new Error(
        `Google Play lookup failed (${playError.message}); APKPure fallback lookup also failed (${fallbackError.message})`
      );
    }
  }
}

module.exports = { resolvePreferredVersion };
