const crypto = require("node:crypto");
const { config, githubJson, githubRequest, resolveModuleCommit, onceworldModuleConfig, allrelicModuleConfig } = require("../../api/_shared/github");
const { resolveLatestPlayListing } = require("../../api/_shared/googleplay");
const { resolveLatestXapk, resolveXapkVariant } = require("../../api/_shared/apkpure");
const { mergeReady } = require("./index");
const { transferRecord, cachedRedirect, transferRedirect } = require("./transfer");
const catalog = require("../../catalog.json");
const inputs = require("../../scripts/catalog/inputs.json");
const { recipe } = require("../../api/_shared/recipes");

const branch = "workshop-ready";
const pending = new Map();
const recentlyReady = new Map();

function entry(id) {
  for (const app of catalog.apps) {
    const variant = app.variants.find(v => v.id === id);
    if (variant && !app.statusUrl) return { app, variant };
  }
  return null;
}

async function readFile(cfg, path) {
  try {
    const apiPath = `/repos/${cfg.githubOwner}/${cfg.githubRepo}/contents/${path}?ref=${branch}`;
    const file = await githubJson(cfg, "GET", apiPath);
    if (file.encoding === "none") {
      const raw = await githubRequest(cfg, { method: "GET", apiPath, accept: "application/vnd.github.raw+json" });
      if (raw.status !== 200) throw new Error("Could not read the release index");
      return { data: JSON.parse(raw.body), sha: file.sha };
    }
    return { data: JSON.parse(Buffer.from(file.content, "base64").toString("utf8")), sha: file.sha };
  } catch (error) {
    if (error.status === 404) return {};
    throw error;
  }
}

async function sourceFor(app, v) {
  if (app.id === "allrelic" || v.region === "japan") {
    const source = await resolveLatestXapk(v.packageName);
    return { ...source, source: "apkpure" };
  }
  try {
    return await resolveLatestPlayListing(v.packageName, { country: "us", language: "en" });
  } catch (error) {
    if (app.id === "anothereden") throw error;
    return { ...await resolveLatestXapk(v.packageName), source: "apkpure" };
  }
}

async function moduleFor(cfg, app, v) {
  if (inputs[app.id]) {
    return { sha: inputs[app.id].moduleSha256, recipe: recipe(app.id) };
  }
  const moduleConfig = app.id === "onceworld" ? onceworldModuleConfig(cfg)
    : app.id === "allrelic" ? allrelicModuleConfig(cfg) : cfg;
  const asset = app.id === "onceworld" ? "app-lsposed-release.apk" : `app-${v.flavor || "release"}.apk`;
  const module = await resolveModuleCommit(moduleConfig, "main", { requireAsset: asset });
  if (!module.prebuilt) throw new Error("The selected module build is not ready");
  if (app.id === "anothereden") {
    if (!/^[a-f0-9]{64}$/.test(module.assetDigest || "")) throw new Error("The selected module release has no verified artifact digest");
    return { ...module, recipe: recipe(app.id) };
  }
  return { ...module, recipe: recipe(app.id) };
}

function identity(v, source, module) {
  // Play only exposes the version name and update timestamp in its public listing.
  // The packaging workflow then checks the delivered code and signer.
  const fields = [
    v.id, source.source, source.versionName, source.versionCode || null,
    source.updatedAt || null, module.sha
  ];
  if (module.recipe) fields.push(module.recipe);
  if (module.assetDigest) fields.push(module.assetDigest);
  if (source.source === "google-play") fields.push(inputs.playClientLock);
  return crypto.createHash("sha256").update(JSON.stringify(fields)).digest("hex");
}

function workflow(cfg, app) {
  if (app.id === "anothereden") return cfg.githubWorkflow;
  if (app.id === "onceworld") return cfg.onceworldWorkflow;
  if (app.id === "allrelic") return cfg.allrelicWorkflow;
  return "catalog-ready.yml";
}

function workflowInputs(app, v, candidate, module, nonce) {
  const catalog = JSON.stringify(candidate);
  const { source, version, code } = candidate;
  const versionCode = String(code || "0");
  if (inputs[app.id]) return { candidate: catalog, nonce };
  const common = { nonce, catalog, moduleSha: module.sha };
  if (app.id === "allrelic") return { ...common, versionName: version, versionCode };
  if (app.id === "onceworld") return { ...common, packageName: v.packageName, architecture: "arm64-v8a",
    moduleRef: module.ref, moduleAsset: "app-lsposed-release.apk", expectedVersionName: version,
    metadataSource: source, versionName: source === "apkpure" ? version : "unknown", versionCode };
  return { ...common, region: v.region, variant: v.flavor, moduleSource: "main", moduleRef: module.ref,
    moduleDigest: module.assetDigest || "", expectedRecipe: module.recipe || "",
    packageName: v.packageName, architecture: "arm64-v8a", runtimeTarget: "phone-arm64", sourceKind: source,
    expectedVersionCode: versionCode, expectedVersionName: version, playLocale: v.region === "japan" ? "ja-JP" : "en-US" };
}

function ready(v) {
  return { status: "ready", variant: v.id, version: v.version, versionCode: v.versionCode,
    sizeBytes: v.sizeBytes, sha256: v.sha256, downloadUrl: v.downloadUrl, updatedAt: v.updatedAt,
    ...(v.moduleVersion && { moduleVersion: v.moduleVersion }), ...(v.moduleSha && { moduleSha: v.moduleSha }) };
}

function waiting(v, ticket) {
  return { status: "preparing", variant: v.id, version: ticket.version,
    statusUrl: `/api/catalog/status?variant=${encodeURIComponent(v.id)}&key=${ticket.key}` };
}

async function availableTransfer(cfg, variant, key, ticket) {
  const record = transferRecord(variant, key, ticket);
  const transferUrl = record && await transferRedirect(cfg, variant, record);
  if (!transferUrl) return null;
  const query = new URLSearchParams({ variant: variant.id, key, sha256: record.sha256 });
  return { ...ready({ ...record, id: variant.id, downloadUrl: `/api/catalog/download?${query}` }), transferUrl };
}

async function runState(cfg, app, ticket) {
  const age = Date.now() - ticket.time;
  if (ticket.failed) return "failed";
  if (age < 8000) return "pending";
  const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions`;
  let run;
  if (Number.isSafeInteger(ticket.runId)) {
    run = await githubJson(cfg, "GET", `${base}/runs/${ticket.runId}`);
  } else {
    const query = new URLSearchParams({ event: "workflow_dispatch", branch: cfg.githubRef,
      per_page: "100", created: ">=" + new Date(ticket.time - 60000).toISOString() });
    const runs = await githubJson(cfg, "GET", `${base}/workflows/${workflow(cfg, app)}/runs?${query}`);
    run = runs.workflow_runs?.find(r => r.display_title?.endsWith(ticket.nonce));
  }
  if (!run) return age < 120000 ? "pending" : "failed";
  return run.status === "completed" ? "finished" : age < 30 * 60000 ? "pending" : "failed";
}

function currentVariant(v, records) {
  return mergeReady({ apps: [{ variants: [v] }] }, records).apps[0].variants[0];
}

async function joinRequest(cfg, app, variant, key, ticket) {
  if (ticket?.key !== key) return null;
  const transfer = await availableTransfer(cfg, variant, key, ticket);
  if (transfer) return transfer;
  if (await runState(cfg, app, ticket) === "pending") return waiting(variant, ticket);
  const current = currentVariant(variant, (await readFile(cfg, "ready.json")).data);
  return current.requestKey === key ? ready(current) : null;
}

async function start(cfg, app, v) {
  const [source, module, files] = await Promise.all([
    sourceFor(app, v), moduleFor(cfg, app, v), readFile(cfg, "ready.json")
  ]);
  const records = files.data || {};
  const current = currentVariant(v, records);
  if (!source.versionName || (source.versionCode && Number(source.versionCode) < current.versionCode)) {
    throw new Error("The original release could not be confirmed");
  }
  const key = identity(v, source, module);
  if (current.requestKey === key && current.version === source.versionName
      && (!source.versionCode || current.versionCode === Number(source.versionCode))) return ready(current);
  const path = `requests/${v.id}.json`;
  let claim = await readFile(cfg, path);
  const joined = await joinRequest(cfg, app, v, key, claim.data);
  if (joined) return joined;
  if (source.source === "apkpure" && !inputs[app.id]) {
    await resolveXapkVariant(v.packageName, source.versionCode, "arm64-v8a", source.versionName);
  }
  const time = Date.now();
  const nonce = `${time}-${crypto.randomBytes(4).toString("hex")}`;
  const ticket = { key, version: source.versionName, time, nonce };
  const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  const saveTicket = sha => githubJson(cfg, "PUT", `${base}/contents/${path}`, {
    branch, message: `Request ${v.id} ${source.versionName}`,
    content: Buffer.from(JSON.stringify(ticket)).toString("base64"), ...(sha && { sha })
  });
  let saved;
  // Other editions advance the same Git branch. Retry the small ticket write
  // without repeating store lookups or dispatching a second build.
  for (let attempt = 0; ; attempt++) {
    try {
      saved = await saveTicket(claim.sha);
      break;
    } catch (error) {
      if (![409, 422].includes(error.status) || attempt >= 7) throw error;
      const delay = 50 + Math.random() * Math.min(1000, 100 * 2 ** attempt);
      await new Promise(resolve => setTimeout(resolve, delay));
      claim = await readFile(cfg, path);
      const competing = await joinRequest(cfg, app, v, key, claim.data);
      if (competing) return competing;
      if (claim.data?.key !== key && claim.data?.time > time) {
        return { status: "changed", variant: v.id, version: claim.data.version, message: "The upstream release changed. Retry to check the new version." };
      }
    }
  }
  const candidate = { app: app.id, variant: v.id, version: source.versionName, code: source.versionCode || null,
    source: source.source, module: module.sha, previous: records[v.id]?.sha256 || current.sha256 || null, key,
    ...(module.recipe && { recipe: module.recipe }),
    ...(module.assetDigest && { moduleDigest: module.assetDigest }) };
  try {
    const run = await githubJson(cfg, "POST", `${base}/actions/workflows/${workflow(cfg, app)}/dispatches`, {
      ref: cfg.githubRef, inputs: workflowInputs(app, v, candidate, module, nonce), return_run_details: true
    });
    if (Number.isSafeInteger(run.workflow_run_id)) ticket.runId = run.workflow_run_id;
  } catch (error) {
    if (error.status >= 400 && error.status < 500) {
      ticket.failed = true;
      await saveTicket(saved.content.sha).catch(() => {});
    }
    throw error;
  }
  // The original claim already lets followers locate the run if a concurrent write wins.
  if (ticket.runId) await saveTicket(saved.content.sha).catch(() => {});
  return waiting(v, ticket);
}

async function poll(cfg, app, v, key) {
  const [file, claim] = await Promise.all([readFile(cfg, "ready.json"), readFile(cfg, `requests/${v.id}.json`)]);
  const current = currentVariant(v, file.data);
  if (current.requestKey === key) return ready(current);
  const transfer = await availableTransfer(cfg, v, key, claim.data);
  if (transfer) return transfer;
  if (claim.data?.key === key && await runState(cfg, app, claim.data) === "pending") return waiting(v, claim.data);
  // Publication can finish between the initial read and the Actions status response.
  const latest = currentVariant(v, (await readFile(cfg, "ready.json")).data);
  return latest.requestKey === key ? ready(latest) : { status: "failed", variant: v.id,
    message: "The release could not be prepared or has changed. Check again to retry." };
}

async function download(cfg, v, key, sha256) {
  const cached = cachedRedirect(v, key, sha256);
  if (cached) return cached;
  const [files, claim] = await Promise.all([readFile(cfg, "ready.json"), readFile(cfg, `requests/${v.id}.json`)]);
  const permanent = currentVariant(v, files.data);
  if (permanent.requestKey === key && permanent.sha256 === sha256) return permanent.downloadUrl;
  const record = transferRecord(v, key, claim.data);
  if (record?.sha256 === sha256) {
    const url = await transferRedirect(cfg, v, record);
    if (url) return url;
  }
  // A previously offered file can outlive its request ticket or be promoted during this lookup.
  const tag = `catalog-${v.id}-${sha256.slice(0, 20)}`;
  const base = `repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  try {
    const release = await githubJson(cfg, "GET", `/${base}/releases/tags/${tag}`);
    const asset = release.assets?.find(a => a.digest === `sha256:${sha256}` && a.state === "uploaded"
      && a.name.startsWith(v.id + "-") && /^[\w.-]+\.apks$/.test(a.name));
    const expected = asset && `https://github.com/${cfg.githubOwner}/${cfg.githubRepo}/releases/download/${tag}/${asset.name}`;
    return !release.draft && release.tag_name === tag && expected === asset?.browser_download_url ? expected : null;
  } catch (error) {
    if (error.status !== 404) throw error;
    return null;
  }
}

function handler(action) {
  return async (req, res) => {
    res.setHeader("content-type", "application/json; charset=utf-8");
    res.setHeader("cache-control", "no-store");
    const send = (status, data) => { res.statusCode = status; res.end(JSON.stringify(data)); };
    const method = action === "build" ? "POST" : "GET";
    if (req.method !== method && !(action === "download" && req.method === "HEAD")) {
      res.setHeader("allow", action === "download" ? "GET, HEAD" : method);
      return send(405, { message: "Method not allowed" });
    }
    const url = new URL(req.url, "https://localhost");
    let body = {};
    if (action === "build") {
      if (!/^application\/json(?:;|$)/i.test(req.headers?.["content-type"] || "")) return send(415, { message: "Expected JSON" });
      try { body = typeof req.body === "string" ? JSON.parse(req.body) : req.body; }
      catch { return send(400, { message: "Invalid JSON" }); }
    }
    const item = entry(action === "build" ? body?.variant : url.searchParams.get("variant"));
    const key = url.searchParams.get("key");
    const sha256 = url.searchParams.get("sha256");
    if (!item || (action !== "build" && !/^[a-f0-9]{64}$/.test(key || ""))
        || (action === "download" && !/^[a-f0-9]{64}$/.test(sha256 || ""))) {
      return send(400, { message: "Invalid download request" });
    }
    try {
      const { app, variant } = item;
      if (action === "download") {
        const location = await download(config(), variant, key, sha256);
        if (!location) return send(409, { message: "This edition changed or is being prepared. Check the catalog again." });
        res.statusCode = 302;
        res.setHeader("location", location);
        return res.end();
      }
      let result;
      if (action === "status") {
        result = await poll(config(), app, variant, key);
        if (result.status !== "failed" && !result.transferUrl) res.setHeader("vercel-cdn-cache-control",
          result.status === "ready" ? "public, s-maxage=60" : "public, s-maxage=2");
      }
      else {
        const cached = recentlyReady.get(variant.id);
        if (cached?.expires > Date.now()) result = cached.result;
        else {
          if (!pending.has(variant.id)) pending.set(variant.id, start(config(), app, variant).finally(() => pending.delete(variant.id)));
          result = await pending.get(variant.id);
          if (result.status === "ready" && !result.transferUrl) recentlyReady.set(variant.id, { result, expires: Date.now() + 10000 });
        }
      }
      if (result.status === "changed") return send(409, result);
      return send(result.status === "preparing" ? 202 : 200, result);
    } catch (error) {
      console.error("Catalog request failed", item.variant.id, error.status || error.name);
      return send(503, { status: "failed", message: "The release service is temporarily unavailable. Please retry." });
    }
  };
}

module.exports = { build: handler("build"), status: handler("status"), download: handler("download"), entry, identity, workflowInputs };
