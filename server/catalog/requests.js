const crypto = require("node:crypto");
const { config, githubJson, resolveModuleCommit, onceworldModuleConfig, allrelicModuleConfig } = require("../../api/_shared/github");
const { resolveLatestPlayListing } = require("../../api/_shared/googleplay");
const { resolveLatestXapk, resolveXapkVariant } = require("../../api/_shared/apkpure");
const { mergeReady } = require("./index");
const catalog = require("../../catalog.json");
const inputs = require("../../scripts/catalog/inputs.json");

const branch = "workshop-ready";
const pending = new Map();

function entry(id) {
  for (const app of catalog.apps) {
    const variant = app.variants.find(v => v.id === id);
    if (variant && !app.statusUrl) return { app, variant };
  }
  return null;
}

async function readFile(cfg, path) {
  try {
    const file = await githubJson(cfg, "GET", `/repos/${cfg.githubOwner}/${cfg.githubRepo}/contents/${path}?ref=${branch}`);
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
  if (inputs[app.id]) return { sha: inputs[app.id].moduleSha256 };
  const moduleConfig = app.id === "onceworld" ? onceworldModuleConfig(cfg)
    : app.id === "allrelic" ? allrelicModuleConfig(cfg) : cfg;
  const asset = app.id === "onceworld" ? "app-lsposed-release.apk" : `app-${v.flavor || "release"}.apk`;
  const module = await resolveModuleCommit(moduleConfig, "main", { requireAsset: asset });
  if (!module.prebuilt) throw new Error("The selected module build is not ready");
  return module;
}

function identity(v, source, module) {
  // Play only exposes the version name and update timestamp in its public listing.
  // The packaging workflow then checks the delivered code and signer.
  return crypto.createHash("sha256").update(JSON.stringify([
    v.id, source.source, source.versionName, source.versionCode || null,
    source.updatedAt || null, module.sha
  ])).digest("hex");
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

async function runState(cfg, app, ticket) {
  const age = Date.now() - ticket.time;
  if (age < 8000) return "pending";
  const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}/actions/workflows/${workflow(cfg, app)}`;
  const query = new URLSearchParams({ event: "workflow_dispatch", branch: cfg.githubRef,
    per_page: "100", created: ">=" + new Date(ticket.time - 60000).toISOString() });
  const runs = await githubJson(cfg, "GET", `${base}/runs?${query}`);
  const run = runs.workflow_runs?.find(r => r.display_title?.endsWith(ticket.nonce));
  if (!run) return age < 120000 ? "pending" : "failed";
  return run.status === "completed" ? "finished" : age < 30 * 60000 ? "pending" : "failed";
}

function currentVariant(v, records) {
  return mergeReady({ apps: [{ variants: [v] }] }, records).apps[0].variants[0];
}

async function start(cfg, app, v, attempt = 0) {
  const [source, module, files] = await Promise.all([
    sourceFor(app, v), moduleFor(cfg, app, v), readFile(cfg, "ready.json")
  ]);
  const records = files.data || {};
  const current = currentVariant(v, records);
  if (!source.versionName || (source.versionCode && Number(source.versionCode) < current.versionCode)) {
    throw new Error("The original release could not be confirmed");
  }
  const key = identity(v, source, module);
  if (current.requestKey === key) return ready(current);
  const path = `requests/${v.id}.json`;
  const claim = await readFile(cfg, path);
  if (claim.data?.key === key && await runState(cfg, app, claim.data) === "pending") {
    return waiting(v, claim.data);
  }
  if (source.source === "apkpure" && !inputs[app.id]) {
    await resolveXapkVariant(v.packageName, source.versionCode, "arm64-v8a", source.versionName);
  }
  const time = Date.now();
  const nonce = `${time}-${crypto.randomBytes(4).toString("hex")}`;
  const ticket = { key, version: source.versionName, time, nonce };
  const base = `/repos/${cfg.githubOwner}/${cfg.githubRepo}`;
  try {
    await githubJson(cfg, "PUT", `${base}/contents/${path}`, {
      branch, message: `Request ${v.id} ${source.versionName}`,
      content: Buffer.from(JSON.stringify(ticket)).toString("base64"), ...(claim.sha && { sha: claim.sha })
    });
  } catch (error) {
    if ([409, 422].includes(error.status) && attempt < 2) return start(cfg, app, v, attempt + 1);
    throw error;
  }
  const candidate = { app: app.id, variant: v.id, version: source.versionName, code: source.versionCode || null,
    source: source.source, module: module.sha, previous: current.sha256, key };
  await githubJson(cfg, "POST", `${base}/actions/workflows/${workflow(cfg, app)}/dispatches`, {
    ref: cfg.githubRef, inputs: workflowInputs(app, v, candidate, module, nonce)
  });
  return waiting(v, ticket);
}

async function poll(cfg, app, v, key) {
  const [file, claim] = await Promise.all([readFile(cfg, "ready.json"), readFile(cfg, `requests/${v.id}.json`)]);
  const current = currentVariant(v, file.data);
  if (current.requestKey === key) return ready(current);
  if (claim.data?.key === key && await runState(cfg, app, claim.data) === "pending") return waiting(v, claim.data);
  // Publication can finish between the initial read and the Actions status response.
  const latest = currentVariant(v, (await readFile(cfg, "ready.json")).data);
  return latest.requestKey === key ? ready(latest) : { status: "failed", variant: v.id,
    message: "The release could not be prepared or has changed. Check again to retry." };
}

function handler(action) {
  return async (req, res) => {
    res.setHeader("content-type", "application/json; charset=utf-8");
    res.setHeader("cache-control", "no-store");
    const send = (status, data) => { res.statusCode = status; res.end(JSON.stringify(data)); };
    const method = action === "build" ? "POST" : "GET";
    if (req.method !== method) {
      res.setHeader("allow", method);
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
    if (!item || (action === "status" && !/^[a-f0-9]{64}$/.test(key || ""))) return send(400, { message: "Invalid download request" });
    try {
      const { app, variant } = item;
      let result;
      if (action === "status") result = await poll(config(), app, variant, key);
      else {
        if (!pending.has(variant.id)) pending.set(variant.id, start(config(), app, variant).finally(() => pending.delete(variant.id)));
        result = await pending.get(variant.id);
      }
      return send(result.status === "preparing" ? 202 : 200, result);
    } catch (error) {
      console.error("Catalog request failed", item.variant.id, error.status || error.name);
      return send(503, { status: "failed", message: "The release service is temporarily unavailable. Please retry." });
    }
  };
}

module.exports = { build: handler("build"), status: handler("status"), entry, identity, workflowInputs };
