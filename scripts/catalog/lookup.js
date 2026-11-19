const { resolveLatestPlayListing } = require("../../api/_shared/googleplay");
const { resolveLatestXapk } = require("../../api/_shared/apkpure");

async function lookup(packageName, strategy) {
  if (strategy !== "apkpure") {
    try {
      return await resolveLatestPlayListing(packageName, { country: "us", language: "en" });
    } catch {
      // Some titles are only distributed outside the US Play catalog.
    }
  }
  const { versionCode, versionName } = await resolveLatestXapk(packageName);
  return { packageName, versionCode, versionName, source: "apkpure" };
}

if (require.main === module) {
  lookup(...process.argv.slice(2)).then(value => console.log(JSON.stringify(value))).catch(error => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
