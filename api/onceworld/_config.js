function onceworldConfig() {
  return {
    packageName: process.env.ONCEWORLD_PACKAGE_NAME || "work.ponix.onceworld",
    architecture: process.env.ONCEWORLD_ARCHITECTURE || "arm64-v8a",
    moduleAsset: process.env.ONCEWORLD_MODULE_ASSET || "app-lsposed-release.apk",
    playCountry: process.env.ONCEWORLD_PLAY_COUNTRY || "us",
    playLanguage: process.env.ONCEWORLD_PLAY_LANGUAGE || "en"
  };
}

module.exports = { onceworldConfig };
