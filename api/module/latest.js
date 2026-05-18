const https = require("https");

function config() {
  return {
    githubToken: process.env.GITHUB_TOKEN || process.env.GH_TOKEN || "",
    owner: process.env.AE_MODULE_REPO_OWNER || "Jordan231111",
    repo: process.env.AE_MODULE_REPO_NAME || "ae-pcd-stamp-tracer",
    ref: process.env.AE_MODULE_REF || "main"
  };
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
  try {
    const commit = await githubJson(cfg,
      `/repos/${cfg.owner}/${cfg.repo}/commits/${encodeURIComponent(cfg.ref)}`);
    if (!commit || typeof commit.sha !== "string" || commit.sha.length < 7) {
      throw new Error(`Could not resolve ${cfg.owner}/${cfg.repo}@${cfg.ref}`);
    }
    const firstLine = commit.commit && commit.commit.message
      ? String(commit.commit.message).split("\n")[0]
      : "";

    res.statusCode = 200;
    res.setHeader("content-type", "application/json");
    res.setHeader("cache-control", "no-store");
    res.end(JSON.stringify({
      owner: cfg.owner,
      repo: cfg.repo,
      ref: cfg.ref,
      sha: commit.sha,
      shortSha: commit.sha.slice(0, 7),
      message: firstLine,
      date: commit.commit && commit.commit.committer ? commit.commit.committer.date : "",
      htmlUrl: commit.html_url || `https://github.com/${cfg.owner}/${cfg.repo}/commit/${commit.sha}`
    }));
  } catch (error) {
    res.statusCode = 500;
    res.setHeader("content-type", "application/json");
    res.setHeader("cache-control", "no-store");
    res.end(JSON.stringify({ message: error.message }));
  }
};
