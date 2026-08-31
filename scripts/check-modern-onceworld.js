const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");
const requireText = (text, needle, message) => {
  if (!text.includes(needle)) throw new Error(message);
};
const forbidText = (text, needle, message) => {
  if (text.includes(needle)) throw new Error(message);
};

const config = read("api/onceworld/_config.js");
const route = read("api/onceworld/build.js");
const versionRoute = read("api/onceworld/version.js");
const versionSource = read("api/onceworld/_version-source.js");
const playMetadata = read("api/_shared/googleplay.js");
const sourceFetcher = read("scripts/fetch-onceworld-source.sh");
const playDownloader = read("scripts/download-onceworld-play.py");
const workflow = read(".github/workflows/build-onceworld-apks.yml");
const frontend = read("index.html");

requireText(config, '"app-lsposed-release.apk"', "OnceWorld must request the modern prebuilt module");
forbidText(config, "app-lspatch-release.apk", "classic OnceWorld module asset returned");
requireText(route, "preferPrebuilt: true", "OnceWorld must stay on the prebuilt fast path");
requireText(route, "requireAsset: game.moduleAsset", "resolved commits must own the requested asset");
requireText(route, "expectedVersionName", "OnceWorld dispatch must pin the preferred release name");
requireText(route, "metadataSource", "OnceWorld dispatch must identify its metadata source");
requireText(versionRoute, "resolvePreferredVersion", "OnceWorld version route must use the preferred source resolver");
requireText(versionSource, "resolveLatestPlayListing", "Google Play must be the preferred metadata source");
requireText(versionSource, "resolveLatestXapk", "APKPure metadata fallback must remain available");
requireText(playMetadata, 'source: "google-play"', "Google Play metadata must identify its source");
requireText(playMetadata, "AF_initDataCallback", "Google Play metadata parser must use listing data");

for (const needle of [
  "gplaydl==4.2.1",
  "download-onceworld-play.py",
  "onceworld_prepare_google_play",
  "onceworld_prepare_apkpure",
  "Google Play primary failed",
  "APKPure is still on",
  "manifest.json"
]) {
  requireText(sourceFetcher, needle, `OnceWorld source fallback contract missing: ${needle}`);
}
for (const needle of [
  '"--max-concurrent-downloads=4"',
  '"--max-connection-per-server=16"',
  '"--split=16"',
  "gzipped_url",
  "Google Play hash mismatch"
]) {
  requireText(playDownloader, needle, `Optimized Google Play downloader contract missing: ${needle}`);
}
if (sourceFetcher.indexOf("onceworld_prepare_google_play") > sourceFetcher.indexOf("onceworld_prepare_apkpure")) {
  throw new Error("Google Play must remain ahead of APKPure in the source fallback chain");
}

for (const needle of [
  "LSPATCH_VERSION: v1.2-487",
  "d238fdc414d121b7fa454d8b4ccf420df3a8c97d563761861ff92bd9c5da2165",
  "gh release download",
  "actions/setup-python@",
  "GPLAYDL_API_KEY",
  "ONCEWORLD_SOURCE_CERT_SHA256",
  "source scripts/fetch-onceworld-source.sh",
  "SOURCE_PID",
  "MODULE_PID",
  "PATCHER_PID",
  "META-INF/xposed/java_init.list",
  "minApiVersion=102",
  "targetApiVersion=102",
  "API_CODE == 102",
  "VERSION_CODE == 487",
  'VERSION_NAME == "1.2"'
]) {
  requireText(workflow, needle, `OnceWorld workflow contract missing: ${needle}`);
}

forbidText(workflow, "./gradlew", "user-request workflow must not compile the Android module");
forbidText(workflow, "app-lspatch-release.apk", "workflow still names the classic module artifact");
forbidText(workflow, "minApiVersion=93", "workflow still accepts API 93");
requireText(frontend, "Google Play is tried first", "OnceWorld UI must describe the source order");

console.log("PASS: OnceWorld fetches Play, the prebuilt module, and pinned LSPatch in parallel with a guarded APKPure fallback.");
