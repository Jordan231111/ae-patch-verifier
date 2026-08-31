const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");
const requireText = (text, needle, message) => {
  if (!text.includes(needle)) throw new Error(message);
};
const forbidPattern = (text, pattern, message) => {
  if (pattern.test(text)) throw new Error(message);
};

const route = read("api/lspatch/build.js");
const workflow = read(".github/workflows/build-lspatched-apks.yml");
const downloader = read("scripts/download-play-apks.py");
const frontend = read("index.html");

forbidPattern(route, /apkpure/i, "Another Eden API must not reference APKPure");
forbidPattern(workflow, /apkpure/i, "Another Eden workflow must not reference APKPure");

for (const needle of [
  'packageName: "games.wfs.anothereden"',
  'packageName: "net.wrightflyer.anothereden"',
  'architecture: "arm64-v8a"',
  'architecture: "x86_64"',
  "resolveLatestPlayListing",
  "expectedVersionName",
  "moduleSource"
]) {
  requireText(route, needle, `Another Eden route contract missing: ${needle}`);
}

for (const needle of [
  "download-play-apks.py",
  "GPLAYDL_GLOBAL_EMAIL",
  "GPLAYDL_JAPAN_EMAIL",
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

for (const needle of [
  '"--max-connection-per-server=16"',
  '"--split=16"',
  "gzipped_url",
  "Google Play hash mismatch"
]) {
  requireText(downloader, needle, `Google Play downloader contract missing: ${needle}`);
}

requireText(frontend, "Direct from Google Play", "Another Eden UI must identify Google Play as its source");
console.log("PASS: all four Another Eden region/ABI targets use direct Google Play with no APKPure runtime path.");
