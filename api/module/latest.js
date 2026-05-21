const https = require("https");

function config() {
  return {
    githubToken: process.env.GITHUB_TOKEN || process.env.GH_TOKEN || "",
    owner: process.env.AE_MODULE_REPO_OWNER || "",
    repo: process.env.AE_MODULE_REPO_NAME || "",
    ref: process.env.AE_MODULE_REF || "main",
    houdiniRef: process.env.AE_MODULE_HOUDINI_REF || "houdini-x64-rewrite"
  };
}

function normalizeModuleSource(value) {
  return value === "houdini-x64-rewrite" ? "houdini-x64-rewrite" : "main";
}

function moduleSourceRef(cfg, moduleSource) {
  return moduleSource === "houdini-x64-rewrite" ? cfg.houdiniRef : cfg.ref;
}

function moduleSourceLabel(moduleSource) {
  return moduleSource === "houdini-x64-rewrite" ? "Houdini x64 rewrite" : "Main";
}

function githubJson(cfg, apiPath) {
  return new Promise((resolve, reject) => {
    const headers = {
      "accept": "application/vnd.github+json",
      "user-agent": "AE Patch Builder",
      "x-github-api-version": "2022-11-28"
    };
    if (cfg.githubToken) headers.authorization = `Bearer ${cfg.githubToken}`;

    const req = https.request({
      hostname: "api.github.com",
      path: apiPath,
      method: "GET",
      headers
    }, response => {
      let text = "";
      response.setEncoding("utf8");
      response.on("data", chunk => { text += chunk; });
      response.on("end", () => {
        if (response.statusCode >= 200 && response.statusCode < 300) {
          try {
            resolve(JSON.parse(text));
          } catch (error) {
            reject(new Error(`GitHub returned non-JSON response: ${text.slice(0, 300)}`));
          }
          return;
        }
        reject(new Error(`GitHub API failed HTTP ${response.statusCode}: ${text.slice(0, 500)}`));
      });
    });
    req.on("error", reject);
    req.end();
  });
}

module.exports = async function handler(req, res) {
  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("allow", "GET");
    res.end("Method Not Allowed");
    return;
  }

  const cfg = config();
  const requestUrl = new URL(req.url || "/", "http://localhost");
  const moduleSource = normalizeModuleSource(requestUrl.searchParams.get("moduleSource"));
  const ref = moduleSourceRef(cfg, moduleSource);
  try {
    if (!cfg.owner || !cfg.repo) {
      throw new Error("Module commit source is not configured");
    }
    const commit = await githubJson(cfg,
      `/repos/${cfg.owner}/${cfg.repo}/commits/${encodeURIComponent(ref)}`);
    if (!commit || typeof commit.sha !== "string" || commit.sha.length < 7) {
      throw new Error(`Could not resolve latest ${moduleSourceLabel(moduleSource)} module commit`);
    }

    res.statusCode = 200;
    res.setHeader("content-type", "application/json");
    res.setHeader("cache-control", "no-store");
    res.end(JSON.stringify({
      shortSha: commit.sha.slice(0, 7),
      moduleSource,
      ref
    }));
  } catch (error) {
    res.statusCode = 500;
    res.setHeader("content-type", "application/json");
    res.setHeader("cache-control", "no-store");
    res.end(JSON.stringify({ message: "Could not resolve latest module commit" }));
  }
};
