const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");
const requireText = (text, needle, message) => {
  if (!text.includes(needle)) throw new Error(message);
};
const route = read("api/lspatch/build.js");
const workflow = read(".github/workflows/build-lspatched-apks.yml");
const downloader = read("scripts/download-play-apks.py");
const frontend = read("index.html");

for (const needle of [
  'packageName: "games.wfs.anothereden"',
  'packageName: "net.wrightflyer.anothereden"',
  'architecture: "arm64-v8a"',
  'runtimeTarget: "houdini-x86_64"',
  "resolveLatestPlayListing",
  "expectedVersionName",
  "moduleSource"
]) {
  requireText(route, needle, `Another Eden route contract missing: ${needle}`);
}
requireText(route, 'source: "google-play"', "Global Another Eden must use Google Play");
requireText(route, 'source: "apkpure"', "Japan Another Eden must use the explicit APKPure source");

for (const needle of [
  "download-play-apks.py",
  "download-apkpure-xapk.sh",
  "GPLAYDL_GLOBAL_EMAIL",
  "AE_GLOBAL_SOURCE_CERT_SHA256",
  "AE_JAPAN_SOURCE_CERT_SHA256",
  "SOURCE_PID",
  "MODULE_PID",
  "PATCHER_PID",
  "config.${ARCHITECTURE//-/_}",
  "assets/lspatch/so/${ARCHITECTURE}/liblspatch.so"
]) {
  requireText(workflow, needle, `Another Eden workflow contract missing: ${needle}`);
}
requireText(workflow, "houdini-x86_64) TARGET_SUFFIX=_emulator-x86_64", "Houdini outputs must retain their emulator filename suffix");

for (const needle of [
  '"--max-connection-per-server=16"',
  '"--split=16"',
  "gzipped_url",
  "Google Play hash mismatch"
]) {
  requireText(downloader, needle, `Google Play downloader contract missing: ${needle}`);
}

requireText(frontend, "Direct from Google Play", "Global Another Eden UI must identify Google Play as its source");
requireText(frontend, "APKPure", "Japan Another Eden UI must identify its explicit fallback source");
console.log("PASS: Global AE uses Google Play and JP AE uses only the explicitly selected optimized APKPure path.");
