const assert = require("node:assert/strict");
const test = require("node:test");

const {
  ApkPureVariantUnavailableError,
  parseXapkRedirect,
  variantUrl
} = require("../api/_shared/apkpure.js");

const PACKAGE_NAME = "net.wrightflyer.anothereden";

function downloadLocation({
  packageName = PACKAGE_NAME,
  versionCode = "1003",
  versionName = "3.16.70",
  fullSize = "195172517",
  host = "data.winudf.com"
} = {}) {
  const token = Buffer.from(
    `${packageName}_${versionCode}_ac31701a`,
    "utf8"
  ).toString("base64url");
  const url = new URL(`https://${host}/XAPK/${token}`);
  url.searchParams.set("filename", `Another_Eden_${versionName}_APKPure.xapk`);
  url.searchParams.set("full_size", fullSize);
  return url;
}

test("variantUrl pins a concrete version and architecture", () => {
  assert.equal(
    variantUrl(PACKAGE_NAME, "1003", "arm64-v8a"),
    "https://d.apkpure.net/b/XAPK/net.wrightflyer.anothereden?versionCode=1003&nc=arm64-v8a"
  );
});

test("parseXapkRedirect accepts a matching trusted CDN object", () => {
  const result = parseXapkRedirect(downloadLocation(), PACKAGE_NAME, "1003");

  assert.equal(result.packageName, PACKAGE_NAME);
  assert.equal(result.versionCode, "1003");
  assert.equal(result.versionName, "3.16.70");
  assert.equal(result.fullSize, 195172517);
  assert.match(result.downloadUrl, /^https:\/\/data\.winudf\.com\/XAPK\//);
});

test("parseXapkRedirect rejects APKPure landing-page redirects", () => {
  assert.throws(
    () => parseXapkRedirect(
      new URL("https://apkpure.com/app/net.wrightflyer.anothereden/versions"),
      PACKAGE_NAME,
      "1003"
    ),
    error => {
      assert.equal(error.code, "APKPURE_VARIANT_UNAVAILABLE");
      assert.match(error.message, /versions page instead of an XAPK/);
      return true;
    }
  );
});

test("parseXapkRedirect rejects mismatched versions and invalid sizes", () => {
  assert.throws(
    () => parseXapkRedirect(downloadLocation({ versionCode: "1000" }), PACKAGE_NAME, "1003"),
    /versionCode 1000 instead of 1003/
  );
  assert.throws(
    () => parseXapkRedirect(downloadLocation({ fullSize: "unknown" }), PACKAGE_NAME, "1003"),
    /valid download size/
  );
});

test("variant-unavailable errors are retryable service failures", () => {
  const error = new ApkPureVariantUnavailableError({
    packageName: PACKAGE_NAME,
    versionCode: "1003",
    versionName: "3.16.70",
    architecture: "arm64-v8a",
    cause: new Error("redirect token does not match")
  });

  assert.equal(error.code, "APKPURE_VARIANT_UNAVAILABLE");
  assert.equal(error.statusCode, 503);
  assert.match(error.message, /has not published an XAPK .* with arm64-v8a/);
});
