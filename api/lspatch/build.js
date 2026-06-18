const fs = require("fs");
const os = require("os");
const path = require("path");
const https = require("https");
const crypto = require("crypto");
const { spawn } = require("child_process");
const {
  config: sharedConfig,
  normalizeModuleSource,
  moduleSourceRef,
  moduleSourceLabel,
  moduleFilenamePart,
  githubJson,
  resolveModuleCommit
} = require("../_shared/github.js");

// APKPure recently re-enabled the 32-bit armeabi-v7a variant, and its `?version=latest`
// endpoint now resolves to that 32-bit build by default. A 32-bit XAPK fails to install on
// arm64-only emulators (INSTALL_FAILED_NO_MATCHING_ABIS) and makes LSPatch's ShadowHook
// inline-hook init fail on real arm64 devices, so we must pin the arm64-v8a native-code split.
const APKPURE_ABI = "arm64-v8a";

const GAMES = {
  global: {
    packageName: "games.wfs.anothereden",
    defaultName: "AnotherEden_Global"
  },
  japan: {
    packageName: "net.wrightflyer.anothereden",
    defaultName: "AnotherEden_Japan"
  }
};

// APKPure ignores the `nc` (native-code/ABI) filter whenever `version=latest` is present — it
// always serves the default variant. The only way to force arm64-v8a is to request a concrete
// versionCode together with `nc`. There is no single "latest + arm64" URL, so we mirror what
// apkpure.com's own download buttons do: resolve the latest versionCode, then pin the ABI.
function apkpureXapkUrl(packageName, versionCode, abi) {
  // Deliberately NO `sv` (device SDK level) param: APKPure serves the requested versionCode's
  // arm64 splits without it, and supplying it couples us to the app's minSdk — APKPure rejects
  // any `sv` below minSdk (returns the app landing page instead of the file). Omitting it is
  // assumption-free and won't break if the game ever raises its minSdk.
  const params = new URLSearchParams({ versionCode: String(versionCode), nc: abi });
  return `https://d.apkpure.net/b/XAPK/${packageName}?${params.toString()}`;
}

// Read APKPure's `?version=latest` 302 WITHOUT downloading the ~200 MB file. The redirect target's
// LAST path segment (before the query string) is base64url("<pkg>_<versionCode>_<hash>"); we parse
// the versionCode out of it, then re-request that exact version pinned to arm64-v8a. The host/path
// is NOT stable -- APKPure has moved this file from d.apkpure.net/b/XAPK/<token> to
// data.winudf.com/XAPK/<token> -- so read the token as "last path segment", never a fixed regex.
function resolveLatestVersionCode(packageName) {
  const url = `https://d.apkpure.net/b/XAPK/${packageName}?version=latest`;
  return new Promise((resolve, reject) => {
    const request = https.get(url, {
      headers: { "user-agent": "Mozilla/5.0 AE Patch Builder", accept: "*/*" }
    }, response => {
      response.resume(); // discard body; we only need the redirect target
      const location = response.headers.location;
      if (response.statusCode < 300 || response.statusCode >= 400 || !location) {
        reject(new Error(`APKPure latest lookup returned HTTP ${response.statusCode} (expected redirect)`));
        return;
      }
      let token = "";
      try {
        token = new URL(location, url).pathname.split("/").filter(Boolean).pop() || "";
      } catch (_) {
        token = location.split("?")[0].split("/").filter(Boolean).pop() || "";
      }
      const b64 = token.replace(/-/g, "+").replace(/_/g, "/");
      const decoded = Buffer.from(b64, "base64").toString("utf8"); // "<pkg>_<versionCode>_<hash>"
      const versionCode = decoded.split("_").slice(-2)[0];
      if (!/^\d+$/.test(versionCode || "")) {
        reject(new Error(`Could not parse versionCode from APKPure redirect "${location}" (token "${token}" decoded "${decoded}")`));
        return;
      }
      resolve(versionCode);
    });
    request.setTimeout(30000, () => request.destroy(new Error("APKPure latest lookup timed out")));
    request.on("error", reject);
  });
}

function config() {
  return {
    ...sharedConfig(),
    lspatchJar: process.env.LSPATCH_JAR || "[removed-private-value]/Tools/RE/lspatch/lspatch-v0.8.jar",
    signerJar: process.env.UBER_APK_SIGNER_JAR || "[removed-private-value]/Downloads/uber-apk-signer.jar",
    keystore: process.env.ASHFUR_KEYSTORE || "[removed-private-value]/Downloads/Ashfur.jks",
    ksAlias: process.env.ASHFUR_ALIAS || "Ashfur",
    ksPass: process.env.ASHFUR_STORE_PASS || "[removed-private-value]",
    keyPass: process.env.ASHFUR_KEY_PASS || "[removed-private-value]",
    moduleRelease: process.env.AE_MODULE_RELEASE_APK || "",
    moduleDebug: process.env.AE_MODULE_DEBUG_APK || "",
    houdiniModuleRelease: process.env.AE_MODULE_HOUDINI_RELEASE_APK || process.env.AE_HOUDINI_MODULE_RELEASE_APK || "",
    houdiniModuleDebug: process.env.AE_MODULE_HOUDINI_DEBUG_APK || process.env.AE_HOUDINI_MODULE_DEBUG_APK || ""
  };
}

function moduleApkPath(cfg, moduleSource, moduleVariant) {
  if (moduleSource === "houdini-x64-rewrite") {
    return moduleVariant === "debug" ? cfg.houdiniModuleDebug : cfg.houdiniModuleRelease;
  }
  return moduleVariant === "debug" ? cfg.moduleDebug : cfg.moduleRelease;
}

function requireFile(file, label) {
  if (!file || !fs.existsSync(file)) {
    throw new Error(`${label} not found: ${file || "<unset>"}`);
  }
}

function run(cmd, args, options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, {
      cwd: options.cwd || process.cwd(),
      env: { ...process.env, ...(options.env || {}) },
      stdio: ["ignore", "pipe", "pipe"]
    });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", chunk => { stdout += chunk.toString(); });
    child.stderr.on("data", chunk => { stderr += chunk.toString(); });
    child.on("error", reject);
    child.on("close", code => {
      if (code === 0) resolve({ stdout, stderr });
      else reject(new Error(`${cmd} exited ${code}\n${stdout}\n${stderr}`));
    });
  });
}

function headerFilename(headers, fallback) {
  const cd = headers["content-disposition"] || "";
  const match = /filename\*?=(?:UTF-8''|")?([^";]+)/i.exec(cd);
  if (!match) return fallback;
  try {
    return decodeURIComponent(match[1].replace(/"/g, ""));
  } catch (_) {
    return match[1].replace(/"/g, "");
  }
}

function download(url, target) {
  return new Promise((resolve, reject) => {
    const request = https.get(url, {
      headers: { "user-agent": "Mozilla/5.0 AE Patch Builder", accept: "*/*" }
    }, response => {
      if (response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
        response.resume();
        const next = new URL(response.headers.location, url).toString();
        download(next, target).then(resolve, reject);
        return;
      }
      if (response.statusCode !== 200) {
        response.resume();
        reject(new Error(`Download failed HTTP ${response.statusCode}`));
        return;
      }
      const out = fs.createWriteStream(target);
      response.pipe(out);
      out.on("finish", () => {
        out.close(() => resolve({
          filename: headerFilename(response.headers, path.basename(target)),
          bytes: Number(response.headers["content-length"] || 0)
        }));
      });
      out.on("error", reject);
    });
    request.setTimeout(120000, () => request.destroy(new Error("Download timed out")));
    request.on("error", reject);
  });
}

function newestApk(dir, suffix) {
  const files = fs.readdirSync(dir)
    .filter(name => name.endsWith(suffix))
    .map(name => path.join(dir, name))
    .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
  if (!files.length) throw new Error(`No ${suffix} output found in ${dir}`);
  return files[0];
}

function splitInputs(extractedDir) {
  const names = fs.readdirSync(extractedDir).filter(name => name.endsWith(".apk"));
  const base = names.find(name => !name.startsWith("config.") && name !== "AssetPack1.apk");
  if (!base) throw new Error("No base APK found in XAPK");
  return {
    base: path.join(extractedDir, base),
    splits: names.filter(name => name !== base).map(name => path.join(extractedDir, name)),
    manifest: path.join(extractedDir, "manifest.json")
  };
}

async function buildApksLocal({ region, moduleVariant, moduleSource }) {
  const game = GAMES[region] || GAMES.global;
  const cfg = config();
  const moduleApk = moduleApkPath(cfg, moduleSource, moduleVariant);

  requireFile(cfg.lspatchJar, "LSPatch jar");
  requireFile(cfg.signerJar, "uber-apk-signer jar");
  requireFile(cfg.keystore, "Ashfur keystore");
  requireFile(moduleApk, `${moduleSourceLabel(moduleSource)} module APK`);

  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ae-lspatch-"));
  const xapk = path.join(root, "source.xapk");
  const extracted = path.join(root, "xapk");
  const lspatchOut = path.join(root, "lspatch-out");
  const signedBase = path.join(root, "signed-base");
  const resignIn = path.join(root, "resign-in");
  const signedSplits = path.join(root, "signed-splits");
  const bundle = path.join(root, "bundle");
  const outFile = path.join(root, `${game.defaultName}_LSPatched_Ashfur${moduleFilenamePart(moduleSource)}_${moduleVariant}.apks`);

  for (const dir of [extracted, lspatchOut, signedBase, resignIn, signedSplits, bundle]) {
    fs.mkdirSync(dir, { recursive: true });
  }

  const versionCode = await resolveLatestVersionCode(game.packageName);
  const xapkUrl = apkpureXapkUrl(game.packageName, versionCode, APKPURE_ABI);
  const downloaded = await download(xapkUrl, xapk);
  await run("unzip", ["-q", "-o", xapk, "-d", extracted]);
  const inputs = splitInputs(extracted);
  const splitNames = inputs.splits.map(name => path.basename(name));
  if (!splitNames.some(name => /arm64_v8a/i.test(name))) {
    throw new Error(`Downloaded XAPK (versionCode ${versionCode}) has no arm64-v8a split; got [${splitNames.join(", ")}]. APKPure may have changed its default ABI again.`);
  }

  await run("java", [
    "-jar", cfg.lspatchJar,
    "-m", moduleApk,
    "-k", cfg.keystore, cfg.ksPass, cfg.ksAlias, cfg.keyPass,
    "-l", "2",
    "-f",
    "-o", lspatchOut,
    inputs.base
  ]);

  const lspatchedBase = newestApk(lspatchOut, "-lspatched.apk");
  await run("java", [
    "-jar", cfg.signerJar,
    "-a", lspatchedBase,
    "--ks", cfg.keystore,
    "--ksAlias", cfg.ksAlias,
    "--ksPass", cfg.ksPass,
    "--ksKeyPass", cfg.keyPass,
    "--allowResign",
    "--out", signedBase
  ]);

  for (const split of inputs.splits) {
    fs.copyFileSync(split, path.join(resignIn, path.basename(split)));
  }
  if (inputs.splits.length) {
    await run("java", [
      "-jar", cfg.signerJar,
      "-a", resignIn,
      "--ks", cfg.keystore,
      "--ksAlias", cfg.ksAlias,
      "--ksPass", cfg.ksPass,
      "--ksKeyPass", cfg.keyPass,
      "--allowResign",
      "--out", signedSplits
    ]);
  }

  const baseSigned = newestApk(signedBase, "-aligned-signed.apk");
  fs.copyFileSync(baseSigned, path.join(bundle, path.basename(inputs.base)));
  for (const split of inputs.splits) {
    const signedName = path.basename(split, ".apk") + "-aligned-signed.apk";
    fs.copyFileSync(path.join(signedSplits, signedName), path.join(bundle, path.basename(split)));
  }
  if (fs.existsSync(inputs.manifest)) {
    fs.copyFileSync(inputs.manifest, path.join(bundle, "manifest.json"));
  }

  const bundleNames = fs.readdirSync(bundle).filter(name => name.endsWith(".apk") || name === "manifest.json");
  await run("zip", ["-q", "-r", "-0", "-Z", "store", outFile, ...bundleNames], { cwd: bundle });

  return { file: outFile, tempRoot: root, filename: path.basename(outFile), downloaded };
}

async function dispatchGithubBuild({ region, moduleVariant, moduleSource }) {
  const cfg = config();
  if (!cfg.githubOwner || !cfg.githubRepo) {
    throw new Error("GitHub builder repository is not configured");
  }
  // Require the exact variant asset to exist so the dispatched build is guaranteed to hit the
  // prebuilt fast-path (skips the Android SDK/NDK/Gradle compile) and stays under a minute.
  const moduleCommit = await resolveModuleCommit(cfg, moduleSource, {
    preferPrebuilt: true,
    requireAsset: `app-${moduleVariant}.apk`
  });
  const nonce = `${Date.now()}-${crypto.randomBytes(4).toString("hex")}`;
  const game = GAMES[region] || GAMES.global;
  const filename = `${game.defaultName}_LSPatched_Ashfur${moduleFilenamePart(moduleSource)}_${moduleVariant}_${moduleCommit.shortSha}.apks`;
  await githubJson(cfg, "POST",
    `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/workflows/${encodeURIComponent(cfg.githubWorkflow)}/dispatches`,
    {
      ref: cfg.githubRef,
      inputs: {
        region,
        variant: moduleVariant,
        moduleSha: moduleCommit.sha,
        moduleRef: moduleCommit.ref,
        nonce
      }
    }
  );
  return {
    nonce,
    filename,
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

  try {
    const body = typeof req.body === "object" && req.body !== null ? req.body : JSON.parse(req.body || "{}");
    const region = body.region === "japan" ? "japan" : "global";
    const moduleVariant = body.moduleVariant === "debug" ? "debug" : "release";
    const moduleSource = normalizeModuleSource(body.moduleSource);
    const cfg = config();

    if (cfg.builderMode === "github") {
      const dispatched = await dispatchGithubBuild({ region, moduleVariant, moduleSource });
      res.statusCode = 202;
      res.setHeader("content-type", "application/json");
      res.setHeader("cache-control", "no-store");
      res.end(JSON.stringify({
        mode: "github",
        ...dispatched,
        statusUrl: `/api/lspatch/status?nonce=${encodeURIComponent(dispatched.nonce)}`,
        downloadUrl: `/api/lspatch/download?nonce=${encodeURIComponent(dispatched.nonce)}`
      }));
      return;
    }

    const result = await buildApksLocal({ region, moduleVariant, moduleSource });

    res.statusCode = 200;
    res.setHeader("content-type", "application/vnd.android.apks");
    res.setHeader("content-disposition", `attachment; filename="${result.filename}"`);
    res.setHeader("x-apkpure-filename", encodeURIComponent(result.downloaded.filename || ""));
    res.setHeader("x-apkpure-size", String(result.downloaded.bytes || ""));
    res.setHeader("x-module-source", moduleSource);
    res.setHeader("x-module-ref", moduleSourceRef(config(), moduleSource));
    const stream = fs.createReadStream(result.file);
    stream.pipe(res);
    const cleanup = () => fs.rm(result.tempRoot, { recursive: true, force: true }, () => {});
    res.on("finish", cleanup);
    res.on("close", cleanup);
  } catch (error) {
    res.statusCode = 500;
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify({ message: error.message }));
  }
};
