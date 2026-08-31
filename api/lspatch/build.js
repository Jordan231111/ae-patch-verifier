const crypto = require("crypto");
const {
  config,
  normalizeModuleSource,
  moduleFilenamePart,
  githubJson,
  resolveModuleCommit
} = require("../_shared/github.js");
const { resolveLatestPlayListing } = require("../_shared/googleplay.js");
const { resolveLatestXapk } = require("../_shared/apkpure.js");

const GAMES = {
  global: {
    packageName: "games.wfs.anothereden",
    defaultName: "AnotherEden_Global",
    playCountry: "us",
    playLanguage: "en",
    playLocale: "en-US",
    source: "google-play"
  },
  japan: {
    packageName: "net.wrightflyer.anothereden",
    defaultName: "AnotherEden_Japan",
    playCountry: "jp",
    playLanguage: "ja",
    playLocale: "ja-JP",
    source: "apkpure"
  }
};

function targetFor(moduleSource) {
  return moduleSource === "houdini-x64-rewrite"
    ? { architecture: "arm64-v8a", runtimeTarget: "houdini-x86_64", label: "Emulator (x86_64/Houdini)" }
    : { architecture: "arm64-v8a", runtimeTarget: "phone-arm64", label: "Phone (ARM64)" };
}

async function dispatchGithubBuild({ region, moduleVariant, moduleSource }) {
  const cfg = config();
  if (!cfg.githubOwner || !cfg.githubRepo) {
    throw new Error("GitHub builder repository is not configured");
  }

  const game = GAMES[region] || GAMES.global;
  const target = targetFor(moduleSource);
  const releaseLookup = game.source === "apkpure"
    ? resolveLatestXapk(game.packageName)
    : resolveLatestPlayListing(game.packageName, {
        country: game.playCountry,
        language: game.playLanguage
      });
  const [moduleCommit, release] = await Promise.all([
    resolveModuleCommit(cfg, moduleSource, {
      preferPrebuilt: true,
      requireAsset: `app-${moduleVariant}.apk`
    }),
    releaseLookup
  ]);

  const nonce = `${Date.now()}-${crypto.randomBytes(4).toString("hex")}`;
  const filename = `${game.defaultName}_${release.versionName}_LSPatched_Ashfur${moduleFilenamePart(moduleSource)}_${moduleVariant}_${moduleCommit.shortSha}.apks`;
  await githubJson(
    cfg,
    "POST",
    `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/workflows/${encodeURIComponent(cfg.githubWorkflow)}/dispatches`,
    {
      ref: cfg.githubRef,
      inputs: {
        region,
        variant: moduleVariant,
        moduleSource,
        moduleSha: moduleCommit.sha,
        moduleRef: moduleCommit.ref,
        packageName: game.packageName,
        architecture: target.architecture,
        runtimeTarget: target.runtimeTarget,
        sourceKind: game.source,
        expectedVersionCode: release.versionCode ? String(release.versionCode) : "0",
        expectedVersionName: release.versionName,
        playLocale: game.playLocale,
        nonce
      }
    }
  );

  return {
    nonce,
    filename,
    packageName: game.packageName,
    architecture: target.architecture,
    runtimeTarget: target.runtimeTarget,
    targetLabel: target.label,
    versionCode: release.versionCode,
    versionName: release.versionName,
    source: game.source,
    moduleSource,
    moduleShortSha: moduleCommit.shortSha,
    moduleRef: moduleCommit.ref,
    region,
    moduleVariant
  };
}

module.exports = async function handler(req, res) {
  if (req.method !== "POST") {
    res.statusCode = 405;
    res.setHeader("allow", "POST");
    res.end("Method Not Allowed");
    return;
  }

  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");

  try {
    const cfg = config();
    if (cfg.builderMode !== "github") {
      res.statusCode = 409;
      res.end(JSON.stringify({ message: "Direct Google Play builds require the GitHub builder" }));
      return;
    }
    const body = typeof req.body === "object" && req.body !== null
      ? req.body
      : JSON.parse(req.body || "{}");
    const region = body.region === "japan" ? "japan" : "global";
    const moduleVariant = body.moduleVariant === "debug" ? "debug" : "release";
    const moduleSource = normalizeModuleSource(body.moduleSource);
    const dispatched = await dispatchGithubBuild({ region, moduleVariant, moduleSource });
    res.statusCode = 202;
    res.end(JSON.stringify({
      mode: "github",
      ...dispatched,
      statusUrl: `/api/lspatch/status?nonce=${encodeURIComponent(dispatched.nonce)}`,
      downloadUrl: `/api/lspatch/download?nonce=${encodeURIComponent(dispatched.nonce)}`
    }));
  } catch (error) {
    res.statusCode = 500;
    res.end(JSON.stringify({ message: error.message }));
  }
};
