#!/usr/bin/env node
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const native = process.env.AE_NATIVE_DIR ? path.resolve(process.env.AE_NATIVE_DIR) : path.join(root, 'native');
const provenance = JSON.parse(fs.readFileSync(path.join(native, 'provenance.json'), 'utf8'));
let localCommit;
try { localCommit = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim(); } catch { /* Vercel may omit .git. */ }
if (localCommit && process.env.VERCEL_GIT_COMMIT_SHA && localCommit !== process.env.VERCEL_GIT_COMMIT_SHA)
  throw new Error('Vercel source commit differs from the checked-out verifier source');
const verifierCommit = localCommit || process.env.VERCEL_GIT_COMMIT_SHA;
if (!/^[a-f0-9]{40}$/.test(verifierCommit)) throw new Error('A full source commit is required for deployment provenance');
const artifacts = {};
for (const name of ['engine.js', 'engine.wasm', 'coverage.js', 'report.js', 'provenance.json']) {
  artifacts[name] = crypto.createHash('sha256').update(fs.readFileSync(path.join(native, name))).digest('hex');
}
fs.writeFileSync(path.join(native, 'build.json'), JSON.stringify({ schemaVersion: 1, verifierCommit,
  moduleCommit: provenance.moduleCommit, artifacts }, null, 2) + '\n');
