const fs = require("fs");
const os = require("os");
const path = require("path");
const https = require("https");
const crypto = require("crypto");
const { spawn } = require("child_process");

const GAMES = {
  global: {
    packageName: "games.wfs.anothereden",
    downloadUrl: "https://d.apkpure.net/b/XAPK/games.wfs.anothereden?version=latest",
    defaultName: "AnotherEden_Global"
  },
  japan: {
    packageName: "net.wrightflyer.anothereden",
    downloadUrl: "https://d.apkpure.net/b/XAPK/net.wrightflyer.anothereden?version=latest",
    defaultName: "AnotherEden_Japan"
  }
};

function config() {
  return {
    builderMode: process.env.LSPATCH_BUILDER_MODE || "local",
    githubToken: process.env.GITHUB_TOKEN || process.env.GH_TOKEN || "",
    githubOwner: process.env.GITHUB_REPO_OWNER || "Jordan231111",
    githubRepo: process.env.GITHUB_REPO_NAME || "ae-patch-verifier",
    githubRef: process.env.GITHUB_REF_NAME || "main",
    githubWorkflow: process.env.GITHUB_WORKFLOW_FILE || "build-lspatched-apks.yml",
    githubPollMs: Number(process.env.GITHUB_BUILDER_POLL_MS || 5000),
    githubTimeoutMs: Number(process.env.GITHUB_BUILDER_TIMEOUT_MS || 270000),
    lspatchJar: process.env.LSPATCH_JAR || "[removed-private-value]/Tools/RE/lspatch/lspatch-v0.8.jar",
    signerJar: process.env.UBER_APK_SIGNER_JAR || "[removed-private-value]/Downloads/uber-apk-signer.jar",
    keystore: process.env.ASHFUR_KEYSTORE || "[removed-private-value]/Downloads/Ashfur.jks",
    ksAlias: process.env.ASHFUR_ALIAS || "Ashfur",
    ksPass: process.env.ASHFUR_STORE_PASS || "[removed-private-value]",
    keyPass: process.env.ASHFUR_KEY_PASS || "[removed-private-value]",
    moduleRelease: process.env.AE_MODULE_RELEASE_APK ||
      "[removed-private-value]/Downloads/PersonalAppReverse/ae-pcd-stamp-tracer/app/build/outputs/apk/release/app-release.apk",
    moduleDebug: process.env.AE_MODULE_DEBUG_APK ||
      "[removed-private-value]/Downloads/PersonalAppReverse/ae-pcd-stamp-tracer/app/build/outputs/apk/debug/app-debug.apk"
  };
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
    child.stdout.on("data", chunk => {
      stdout += chunk.toString();
      if (options.onStdout) options.onStdout(chunk.toString());
    });
    child.stderr.on("data", chunk => {
      stderr += chunk.toString();
      if (options.onStderr) options.onStderr(chunk.toString());
    });
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
      headers: {
        "user-agent": "Mozilla/5.0 AE Patch Builder",
        "accept": "*/*"
      }
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
    request.setTimeout(120000, () => {
      request.destroy(new Error("Download timed out"));
    });
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

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function githubJson(cfg, method, apiPath, body) {
  if (!cfg.githubToken) throw new Error("GITHUB_TOKEN is required for hosted GitHub builder mode");
  const payload = body === undefined ? undefined : JSON.stringify(body);
  return new Promise((resolve, reject) => {
    const req = https.request({
      hostname: "api.github.com",
      path: apiPath,
      method,
      headers: {
        "accept": "application/vnd.github+json",
        "authorization": `Bearer ${cfg.githubToken}`,
        "user-agent": "AE Patch Builder",
        "x-github-api-version": "2022-11-28",
        ...(payload ? {
          "content-type": "application/json",
          "content-length": Buffer.byteLength(payload)
        } : {})
      }
    }, response => {
      let text = "";
      response.setEncoding("utf8");
      response.on("data", chunk => { text += chunk; });
      response.on("end", () => {
        if (response.statusCode >= 200 && response.statusCode < 300) {
          if (!text.trim()) {
            resolve({});
            return;
          }
          try {
            resolve(JSON.parse(text));
          } catch (error) {
            reject(new Error(`GitHub returned non-JSON response: ${text.slice(0, 300)}`));
          }
          return;
        }
        reject(new Error(`GitHub API ${method} ${apiPath} failed HTTP ${response.statusCode}: ${text.slice(0, 500)}`));
      });
    });
    req.on("error", reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function streamUrl(url, headers, res) {
  return new Promise((resolve, reject) => {
    const req = https.get(url, { headers }, response => {
      if (response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
        response.resume();
        const next = new URL(response.headers.location, url).toString();
        streamUrl(next, headers, res).then(resolve, reject);
        return;
      }
      if (response.statusCode !== 200) {
        let text = "";
        response.setEncoding("utf8");
        response.on("data", chunk => { text += chunk; });
        response.on("end", () => reject(new Error(`Asset download failed HTTP ${response.statusCode}: ${text.slice(0, 500)}`)));
        return;
      }
      response.pipe(res);
      response.on("end", resolve);
      response.on("error", reject);
    });
    req.on("error", reject);
  });
}

async function waitForGithubRun(cfg, nonce) {
  const ownerRepo = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  const runsPath = `${ownerRepo}/actions/workflows/${encodeURIComponent(cfg.githubWorkflow)}/runs?event=workflow_dispatch&branch=${encodeURIComponent(cfg.githubRef)}&per_page=30`;
  const deadline = Date.now() + cfg.githubTimeoutMs;
  while (Date.now() < deadline) {
    const data = await githubJson(cfg, "GET", runsPath);
    const runs = Array.isArray(data.workflow_runs) ? data.workflow_runs : [];
    const run = runs.find(candidate => (candidate.display_title || candidate.name || "").includes(nonce));
    if (run) {
      if (run.status === "completed") {
        if (run.conclusion !== "success") {
          throw new Error(`GitHub builder run failed: ${run.conclusion || "unknown"} (${run.html_url})`);
        }
        return run;
      }
    }
    await sleep(cfg.githubPollMs);
  }
  throw new Error(`Timed out waiting for GitHub builder run ${nonce}`);
}

async function buildApksViaGithub({ region, moduleVariant }, res) {
  const game = GAMES[region] || GAMES.global;
  const cfg = config();
  const nonce = `${Date.now()}-${crypto.randomBytes(4).toString("hex")}`;
  const ownerRepo = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  const workflowPath = `${ownerRepo}/actions/workflows/${encodeURIComponent(cfg.githubWorkflow)}/dispatches`;

  await githubJson(cfg, "POST", workflowPath, {
    ref: cfg.githubRef,
    inputs: {
      region,
      variant: moduleVariant,
      nonce
    }
  });

  await waitForGithubRun(cfg, nonce);
  const release = await githubJson(cfg, "GET", `${ownerRepo}/releases/tags/lspatch-${nonce}`);
  const assets = Array.isArray(release.assets) ? release.assets : [];
  const asset = assets.find(item => item.name && item.name.endsWith(".apks"));
  if (!asset) throw new Error(`GitHub builder finished but no .apks release asset was found for ${nonce}`);

  const filename = `${game.defaultName}_LSPatched_Ashfur_${moduleVariant}.apks`;
  res.statusCode = 200;
  res.setHeader("content-type", "application/vnd.android.apks");
  res.setHeader("content-disposition", `attachment; filename="${filename}"`);
  res.setHeader("x-builder", "github-actions");
  res.setHeader("x-github-release", release.html_url || "");
  res.setHeader("x-github-tag", githubReleaseTag(release) || "");
  try {
    await streamUrl(asset.url, {
      "accept": "application/octet-stream",
      "authorization": `Bearer ${cfg.githubToken}`,
      "user-agent": "AE Patch Builder"
    }, res);
  } finally {
    await cleanupGithubRelease(cfg, release).catch(error => {
      console.warn("GitHub cleanup failed:", error.message);
    });
  }
}

async function cleanupGithubRelease(cfg, release) {
  if (!release || !release.id) return;
  const ownerRepo = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  await githubJson(cfg, "DELETE", `${ownerRepo}/releases/${release.id}`);
  const tagName = githubReleaseTag(release);
  if (tagName) {
    await sleep(15000);
    await deleteGithubTagWithRetry(cfg, ownerRepo, tagName);
  }
}

async function deleteGithubTagWithRetry(cfg, ownerRepo, tagName) {
  const refPath = `${ownerRepo}/git/refs/tags/${encodeURIComponent(tagName)}`;
  let lastError = null;
  for (let attempt = 0; attempt < 8; attempt += 1) {
    try {
      await githubJson(cfg, "DELETE", refPath);
      return;
    } catch (error) {
      lastError = error;
      await sleep(1000);
    }
  }
  if (lastError) throw lastError;
}

function githubReleaseTag(release) {
  if (!release) return "";
  if (release.tag_name) return release.tag_name;
  if (release.tagName) return release.tagName;
  const match = /\/tag\/([^/?#]+)/.exec(release.html_url || release.url || "");
  return match ? decodeURIComponent(match[1]) : "";
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

async function buildApks({ region, moduleVariant }) {
  const game = GAMES[region] || GAMES.global;
  const cfg = config();
  const moduleApk = moduleVariant === "debug" ? cfg.moduleDebug : cfg.moduleRelease;

  requireFile(cfg.lspatchJar, "LSPatch jar");
  requireFile(cfg.signerJar, "uber-apk-signer jar");
  requireFile(cfg.keystore, "Ashfur keystore");
  requireFile(moduleApk, "Module APK");

  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ae-lspatch-"));
  const xapk = path.join(root, "source.xapk");
  const extracted = path.join(root, "xapk");
  const lspatchOut = path.join(root, "lspatch-out");
  const signedBase = path.join(root, "signed-base");
  const resignIn = path.join(root, "resign-in");
  const signedSplits = path.join(root, "signed-splits");
  const bundle = path.join(root, "bundle");
  const outFile = path.join(root, `${game.defaultName}_LSPatched_Ashfur_${moduleVariant}.apks`);

  for (const dir of [extracted, lspatchOut, signedBase, resignIn, signedSplits, bundle]) {
    fs.mkdirSync(dir, { recursive: true });
  }

  const downloaded = await download(game.downloadUrl, xapk);
  await run("unzip", ["-q", "-o", xapk, "-d", extracted]);
  const inputs = splitInputs(extracted);

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

  return {
    file: outFile,
    tempRoot: root,
    filename: path.basename(outFile),
    downloaded
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
    if (config().builderMode === "github") {
      await buildApksViaGithub({ region, moduleVariant }, res);
      return;
    }
    const result = await buildApks({ region, moduleVariant });

    res.statusCode = 200;
    res.setHeader("content-type", "application/vnd.android.apks");
    res.setHeader("content-disposition", `attachment; filename="${result.filename}"`);
    res.setHeader("x-apkpure-filename", encodeURIComponent(result.downloaded.filename || ""));
    res.setHeader("x-apkpure-size", String(result.downloaded.bytes || ""));
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
