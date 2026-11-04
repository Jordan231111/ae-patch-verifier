const { config } = require('../_shared/github.js');

function relevelConfig() {
  const base = config();
  return {
    ...base,
    githubTimeoutMs: Math.min(base.githubTimeoutMs, 8000),
    packageName: 'work.ponix.relevel',
    architecture: 'arm64-v8a',
    moduleOwner: process.env.RELEVEL_MODULE_REPO_OWNER || base.githubOwner,
    moduleRepo: process.env.RELEVEL_MODULE_REPO_NAME || 'ReLevel',
    moduleRef: process.env.RELEVEL_MODULE_REF || 'main',
    readyTag: process.env.RELEVEL_READY_TAG || 'relevel-ready',
    certificateSha256: '29b04c032802179a86374c8a13dba9ddbe8d9303f4909ac03766cb77de17c027'
  };
}
module.exports = { relevelConfig };
