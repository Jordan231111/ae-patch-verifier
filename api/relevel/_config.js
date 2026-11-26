const { config } = require('../_shared/github.js');

function relevelConfig() {
  const base = config();
  return {
    ...base,
    githubTimeoutMs: Math.min(base.githubTimeoutMs, 8000),
    packageName: 'work.ponix.relevel',
    architecture: 'arm64-v8a',
    readyTag: process.env.RELEVEL_READY_TAG || 'relevel-ready',
    relevelWorkflow: process.env.RELEVEL_GITHUB_WORKFLOW_FILE || 'relevel-ready.yml',
    relevelTimerUrl: process.env.RELEVEL_TIMER_URL || 'https://relevel-release-timer.cursor4free.workers.dev',
    certificateSha256: '5486b776291d98b9bc5a70586b9af89afce8674fd462889238c0ebd99786a1c1'
  };
}
module.exports = { relevelConfig };
