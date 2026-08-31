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
const workflow = read(".github/workflows/build-onceworld-apks.yml");

requireText(config, '"app-lsposed-release.apk"', "OnceWorld must request the modern prebuilt module");
forbidText(config, "app-lspatch-release.apk", "classic OnceWorld module asset returned");
requireText(route, "preferPrebuilt: true", "OnceWorld must stay on the prebuilt fast path");
requireText(route, "requireAsset: game.moduleAsset", "resolved commits must own the requested asset");

for (const needle of [
  "LSPATCH_VERSION: v1.2-487",
  "d238fdc414d121b7fa454d8b4ccf420df3a8c97d563761861ff92bd9c5da2165",
  "gh release download",
  "actions/cache@",
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

console.log("PASS: OnceWorld uses cached LSPatch v1.2 and a prebuilt modern API-102 module.");
