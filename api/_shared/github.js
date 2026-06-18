const https = require("https");

function config() {
  return {
    builderMode: process.env.LSPATCH_BUILDER_MODE || "local",
    githubToken: process.env.GITHUB_TOKEN || process.env.GH_TOKEN || "",
    githubOwner: process.env.GITHUB_REPO_OWNER || "",
    githubRepo: process.env.GITHUB_REPO_NAME || "",
    githubRef: process.env.GITHUB_REF_NAME || "main",
    githubWorkflow: process.env.GITHUB_WORKFLOW_FILE || "build-lspatched-apks.yml",
    moduleOwner: process.env.AE_MODULE_REPO_OWNER || "",
    moduleRepo: process.env.AE_MODULE_REPO_NAME || "",
    moduleRef: process.env.AE_MODULE_REF || "main",
    houdiniModuleRef: process.env.AE_MODULE_HOUDINI_REF || "houdini-x64-rewrite"
  };
}

function normalizeModuleSource(value) {
  return value === "houdini-x64-rewrite" ? "houdini-x64-rewrite" : "main";
}

function moduleSourceRef(cfg, moduleSource) {
  return moduleSource === "houdini-x64-rewrite" ? cfg.houdiniModuleRef : cfg.moduleRef;
}

function moduleSourceLabel(moduleSource) {
  return moduleSource === "houdini-x64-rewrite" ? "Emulator (x86_64)" : "Phone (ARM64)";
}

function moduleFilenamePart(moduleSource) {
  return moduleSource === "houdini-x64-rewrite" ? "_emulator-x86_64" : "";
}

function githubRequest(cfg, { method, apiPath, body, accept, followRedirect = false }) {
  if (!cfg.githubToken) {
    return Promise.reject(new Error("GITHUB_TOKEN is required"));
  }
  const payload = body === undefined ? undefined : JSON.stringify(body);
  return new Promise((resolve, reject) => {
    const req = https.request({
      hostname: "api.github.com",
      path: apiPath,
      method,
      headers: {
        accept: accept || "application/vnd.github+json",
        authorization: `Bearer ${cfg.githubToken}`,
        "user-agent": "AE Patch Builder",
        "x-github-api-version": "2022-11-28",
        ...(payload ? {
          "content-type": "application/json",
          "content-length": Buffer.byteLength(payload)
        } : {})
      }
    }, response => {
      const status = response.statusCode || 0;
      if (!followRedirect && status >= 300 && status < 400 && response.headers.location) {
        response.resume();
        resolve({ status, headers: response.headers, body: "" });
        return;
      }
      let text = "";
      response.setEncoding("utf8");
      response.on("data", chunk => { text += chunk; });
      response.on("end", () => resolve({ status, headers: response.headers, body: text }));
    });
    req.on("error", reject);
    if (payload) req.write(payload);
    req.end();
  });
}

async function githubJson(cfg, method, apiPath, body) {
  const result = await githubRequest(cfg, { method, apiPath, body });
  if (result.status < 200 || result.status >= 300) {
    const snippet = result.body.slice(0, 500);
    const err = new Error(`GitHub API ${method} ${apiPath} failed HTTP ${result.status}: ${snippet}`);
    err.status = result.status;
    throw err;
  }
  if (!result.body.trim()) return {};
  try {
    return JSON.parse(result.body);
  } catch (_) {
    throw new Error(`GitHub returned non-JSON response: ${result.body.slice(0, 300)}`);
  }
}

const MODULE_RELEASE_TAG = /^module-([0-9a-f]{40})$/i;

// Full commit SHAs that already have a durable `module-<sha>` release published by the module
// repo's Android Build CI. Optionally require a specific asset (e.g. "app-release.apk") so we only
// count releases that actually carry the variant the build will download.
async function publishedModuleShas(cfg, asset) {
  const releases = await githubJson(
    cfg,
    "GET",
    `/repos/${cfg.moduleOwner}/${cfg.moduleRepo}/releases?per_page=100`
  );
  const shas = new Set();
  for (const rel of Array.isArray(releases) ? releases : []) {
    const match = MODULE_RELEASE_TAG.exec((rel && rel.tag_name) || "");
    if (!match) continue;
    if (asset && !(rel.assets || []).some(a => a && a.name === asset)) continue;
    shas.add(match[1].toLowerCase());
  }
  return shas;
}

async function resolveModuleCommit(cfg, moduleSource, options = {}) {
  if (!cfg.moduleOwner || !cfg.moduleRepo) {
    throw new Error("Module commit source is not configured");
  }
  const ref = moduleSourceRef(cfg, moduleSource);
  // List recent branch commits (newest first) instead of just HEAD, so we can hand the builder the
  // newest commit that is ACTUALLY prebuilt rather than a bleeding-edge HEAD the module CI may not
  // have compiled yet. The builder skips its ~2-3 min Android compile only when a prebuilt exists,
  // so this is what keeps every dispatched run on the <1 min fast path.
  const commits = await githubJson(
    cfg,
    "GET",
    `/repos/${cfg.moduleOwner}/${cfg.moduleRepo}/commits?sha=${encodeURIComponent(ref)}&per_page=30`
  );
  const head = Array.isArray(commits) ? commits[0] : null;
  if (!head || typeof head.sha !== "string" || head.sha.length < 7) {
    throw new Error(`Could not resolve latest ${moduleSourceLabel(moduleSource)} module commit`);
  }

  let chosen = head;
  let prebuilt = false;
  // Gated to `main` on purpose: main is branch-correct (every main release is a main build),
  // whereas houdini-x64-rewrite shares pre-fork ancestors with main, so walking back there could
  // hand an ARM64 module to an x86_64 build. houdini therefore stays on HEAD and lets the builder
  // compile if its commit is not prebuilt -- correctness wins over the fast path for that variant.
  if (moduleSource === "main" && options.preferPrebuilt !== false) {
    const shas = await publishedModuleShas(cfg, options.requireAsset).catch(() => new Set());
    const withPrebuilt = commits.find(c => c && shas.has(String(c.sha).toLowerCase()));
    if (withPrebuilt) {
      chosen = withPrebuilt;
      prebuilt = true;
    }
  }
  return { sha: chosen.sha, shortSha: chosen.sha.slice(0, 7), ref, prebuilt };
}

module.exports = {
  config,
  normalizeModuleSource,
  moduleSourceRef,
  moduleSourceLabel,
  moduleFilenamePart,
  githubRequest,
  githubJson,
  resolveModuleCommit
};
