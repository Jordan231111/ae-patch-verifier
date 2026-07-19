function onceworldConfig() {
  return {
    packageName: process.env.ONCEWORLD_PACKAGE_NAME || "work.ponix.onceworld",
    architecture: process.env.ONCEWORLD_ARCHITECTURE || "arm64-v8a",
    moduleAsset: process.env.ONCEWORLD_MODULE_ASSET || "app-lspatch-release.apk"
  };
}

module.exports = { onceworldConfig };
