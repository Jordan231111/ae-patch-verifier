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
const { resolveLatestXapk } = require("../_shared/apkpure.js");

// APKPure recently re-enabled the 32-bit armeabi-v7a variant, and its `?version=latest`
// endpoint now resolves to that 32-bit build by default. A 32-bit XAPK fails to install on
// arm64-only emulators (INSTALL_FAILED_NO_MATCHING_ABIS) and makes LSPatch's ShadowHook
// inline-hook init fail on real arm64 devices, so we must pin the arm64-v8a native-code split.
const APKPURE_ABI = "arm64-v8a";
const LSPATCH_RELEASE = Object.freeze({
  versionName: "1.2",
  versionCode: 487,
  apiCode: 102,
  sha256: "d238fdc414d121b7fa454d8b4ccf420df3a8c97d563761861ff92bd9c5da2165"
});

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
  return resolveLatestXapk(packageName).then(latest => latest.versionCode);
}

function config() {
  return {
    ...sharedConfig(),
    // Production dispatches GitHub Actions, but this local rehearsal path remains useful for
    // rehearsals. Require its machine paths and credentials explicitly so a public checkout
    // never contains a developer home path or a credential fallback.
    lspatchJar: process.env.LSPATCH_JAR || "",
    apksigner: process.env.APKSIGNER || androidBuildTool("apksigner"),
    zipalign: process.env.ZIPALIGN || androidBuildTool("zipalign"),
    keystore: process.env.ASHFUR_KEYSTORE || "",
    ksAlias: process.env.ASHFUR_ALIAS || "",
    ksPass: process.env.ASHFUR_STORE_PASS || "",
    keyPass: process.env.ASHFUR_KEY_PASS || "",
    hostCertSha256: process.env.AE_HOST_CERT_SHA256 || "",
    moduleRelease: process.env.AE_MODULE_RELEASE_APK || "",
    moduleDebug: process.env.AE_MODULE_DEBUG_APK || "",
    houdiniModuleRelease: process.env.AE_MODULE_HOUDINI_RELEASE_APK || process.env.AE_HOUDINI_MODULE_RELEASE_APK || "",
    houdiniModuleDebug: process.env.AE_MODULE_HOUDINI_DEBUG_APK || process.env.AE_HOUDINI_MODULE_DEBUG_APK || ""
  };
}

function androidBuildTool(name) {
  const sdkRoot = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT || "";
  const buildTools = path.join(sdkRoot, "build-tools");
  if (!sdkRoot || !fs.existsSync(buildTools)) return "";
  const versions = fs.readdirSync(buildTools)
    .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }));
  for (const version of versions) {
    const candidate = path.join(buildTools, version, name);
    if (fs.existsSync(candidate)) return candidate;
  }
  return "";
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

function sha256File(file) {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

function verifyLspatchJar(file) {
  const actual = sha256File(file);
  if (actual !== LSPATCH_RELEASE.sha256) {
    throw new Error(`LSPatch jar digest mismatch: expected ${LSPATCH_RELEASE.sha256}, got ${actual}`);
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

async function verifyModernMainModule(moduleApk) {
  const entries = (await run("unzip", ["-Z1", moduleApk])).stdout.split(/\r?\n/).filter(Boolean);
  for (const entry of [
    "META-INF/xposed/java_init.list",
    "META-INF/xposed/module.prop",
    "META-INF/xposed/scope.list"
  ]) {
    if (!entries.includes(entry)) throw new Error(`Modern main module is missing ${entry}`);
  }
  if (entries.includes("assets/xposed_init")) {
    throw new Error("Modern main module contains legacy assets/xposed_init");
  }

  const prop = (await run("unzip", ["-p", moduleApk, "META-INF/xposed/module.prop"])).stdout;
  if (!/^minApiVersion=102$/m.test(prop) || !/^targetApiVersion=102$/m.test(prop)) {
    throw new Error("Modern main module must target libxposed API 102");
  }
  const javaEntries = (await run("unzip", ["-p", moduleApk, "META-INF/xposed/java_init.list"])).stdout
    .split(/\r?\n/)
    .map(line => line.trim())
    .filter(line => line && !line.startsWith("#"));
  if (javaEntries.length !== 1) {
    throw new Error(`Modern main module must contain exactly one Java entry; got ${javaEntries.length}`);
  }
}

async function verifyLspatchConfig(apk) {
  const raw = (await run("unzip", ["-p", apk, "assets/lspatch/config.json"])).stdout;
  const config = JSON.parse(raw);
  const actual = config.lspConfig || {};
  if (config.useManager !== false || config.sigBypassLevel !== 2 ||
      actual.API_CODE !== LSPATCH_RELEASE.apiCode ||
      actual.VERSION_CODE !== LSPATCH_RELEASE.versionCode ||
      actual.VERSION_NAME !== LSPATCH_RELEASE.versionName) {
    throw new Error(`Unexpected LSPatch runtime config: ${raw}`);
  }
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

function splitInputs(extractedDir, expectedPackage, expectedVersionCode) {
  const manifestPath = path.join(extractedDir, "manifest.json");
  requireFile(manifestPath, "XAPK manifest");
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
  if (manifest.package_name !== expectedPackage ||
      String(manifest.version_code) !== String(expectedVersionCode)) {
    throw new Error("XAPK manifest does not match the requested package/version");
  }
  if (!Array.isArray(manifest.split_apks)) throw new Error("XAPK manifest has no split list");
  if (!manifest.split_apks.some(split => split.id === "config.arm64_v8a")) {
    throw new Error("XAPK manifest has no arm64-v8a split");
  }
  const base = manifest.split_apks.find(split => split.id === "base");
  if (!base) throw new Error("XAPK manifest has no base split");
  const safeFile = value => typeof value === "string" && /^[A-Za-z0-9._-]+\.apk$/.test(value);
  if (!safeFile(base.file)) throw new Error(`Unsafe base filename in XAPK manifest: ${base.file}`);
  const splits = manifest.split_apks.filter(split => split.id !== "base");
  if (!splits.length) throw new Error("XAPK manifest has no installable splits");
  for (const split of splits) {
    if (!safeFile(split.file)) throw new Error(`Unsafe split filename in XAPK manifest: ${split.file}`);
    requireFile(path.join(extractedDir, split.file), `XAPK split ${split.id}`);
  }
  requireFile(path.join(extractedDir, base.file), "XAPK base");
  return {
    base: path.join(extractedDir, base.file),
    baseName: base.file,
    splits: splits.map(split => path.join(extractedDir, split.file)),
    splitNames: splits.map(split => split.file),
    manifest: manifestPath
  };
}

function normalizeDigest(value) {
  return String(value || "").replace(/:/g, "").toLowerCase();
}

async function verifySigningIdentity(apksigner, apk, expectedDigest) {
  const result = await run(apksigner, ["verify", "--print-certs", apk]);
  const match = /certificate SHA-256 digest:\s*([0-9a-f:]+)/i.exec(result.stdout);
  const actual = normalizeDigest(match && match[1]);
  if (!actual || actual !== normalizeDigest(expectedDigest)) {
    throw new Error(`Signing certificate mismatch for ${path.basename(apk)}: ${actual || "missing"}`);
  }
}

async function buildApksLocal({ region, moduleVariant, moduleSource }) {
  const game = GAMES[region] || GAMES.global;
  const cfg = config();
  const moduleApk = moduleApkPath(cfg, moduleSource, moduleVariant);

  requireFile(cfg.lspatchJar, "LSPatch jar");
  requireFile(cfg.apksigner, "Android apksigner");
  requireFile(cfg.zipalign, "Android zipalign");
  requireFile(cfg.keystore, "Ashfur keystore");
  if (!cfg.ksAlias || !cfg.ksPass || !cfg.keyPass) throw new Error("Ashfur keystore credentials are required");
  if (!normalizeDigest(cfg.hostCertSha256)) throw new Error("AE_HOST_CERT_SHA256 is required");
  requireFile(moduleApk, `${moduleSourceLabel(moduleSource)} module APK`);
  verifyLspatchJar(cfg.lspatchJar);
  if (moduleSource === "main") await verifyModernMainModule(moduleApk);

  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ae-lspatch-"));
  const xapk = path.join(root, "source.xapk");
  const extracted = path.join(root, "xapk");
  const lspatchOut = path.join(root, "lspatch-out");
  const bundle = path.join(root, "bundle");
  const outFile = path.join(root, `${game.defaultName}_LSPatched_Ashfur${moduleFilenamePart(moduleSource)}_${moduleVariant}.apks`);

  for (const dir of [extracted, lspatchOut, bundle]) {
    fs.mkdirSync(dir, { recursive: true });
  }

  const versionCode = await resolveLatestVersionCode(game.packageName);
  const xapkUrl = apkpureXapkUrl(game.packageName, versionCode, APKPURE_ABI);
  const downloaded = await download(xapkUrl, xapk);
  await run("unzip", ["-tq", xapk]);
  await run("unzip", ["-q", "-o", xapk, "-d", extracted]);
  const inputs = splitInputs(extracted, game.packageName, versionCode);

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
  const alignedBase = path.join(root, "base-aligned.apk");
  const finalBase = path.join(bundle, inputs.baseName);
  await run(cfg.zipalign, ["-f", "-P", "16", "4", lspatchedBase, alignedBase]);
  await run(cfg.apksigner, [
    "sign",
    "--v4-signing-enabled", "false",
    "--alignment-preserved", "true",
    "--ks", cfg.keystore,
    "--ks-pass", `pass:${cfg.ksPass}`,
    "--ks-key-alias", cfg.ksAlias,
    "--key-pass", `pass:${cfg.keyPass}`,
    "--out", finalBase,
    alignedBase
  ]);
  fs.rmSync(alignedBase, { force: true });

  for (let i = 0; i < inputs.splits.length; i += 1) {
    const output = path.join(bundle, inputs.splitNames[i]);
    await run(cfg.apksigner, [
      "sign",
      "--v4-signing-enabled", "false",
      "--ks", cfg.keystore,
      "--ks-pass", `pass:${cfg.ksPass}`,
      "--ks-key-alias", cfg.ksAlias,
      "--key-pass", `pass:${cfg.keyPass}`,
      "--out", output,
      inputs.splits[i]
    ]);
  }

  await verifyLspatchConfig(finalBase);
  for (const apkName of [inputs.baseName, ...inputs.splitNames]) {
    await verifySigningIdentity(cfg.apksigner, path.join(bundle, apkName), cfg.hostCertSha256);
  }
  fs.copyFileSync(inputs.manifest, path.join(bundle, "manifest.json"));

  const bundleNames = [inputs.baseName, ...inputs.splitNames, "manifest.json"];
  await run("zip", ["-q", "-0", outFile, ...bundleNames], { cwd: bundle });

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
