#!/usr/bin/env node
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const os = require('node:os');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const native = path.join(root, 'native');
const info = require('../native/provenance.json');
const coverage = require('../native/coverage.js');
assert.equal(info.schemaVersion, 2);
assert.equal(info.uncommittedSource, false, 'Published checks must come from committed module inputs');
assert.match(info.moduleCommit, /^[a-f0-9]{40}$/);
assert.deepEqual(info.staticCoverage, coverage.checks);
for (const [name, expected] of Object.entries({ ...info.generatedFiles, ...info.verifierNativeFiles })) {
  const actual = crypto.createHash('sha256').update(fs.readFileSync(path.join(native, name))).digest('hex');
  assert.equal(actual, expected, name + ' differs from its recorded import; regenerate the engine');
}
for (const name of ['item_catalog_signatures.h', 'item_injection_contracts.h', 'item_injection_queue.h']) {
  assert.equal(info.generatedFiles[name], info.productionFiles['app/src/main/cpp/' + name]);
}
assert.ok(!/\b(step_item_injection|injection_amount|hooked_injection_sync)\s*\(/.test(fs.readFileSync(path.join(native, 'item_injection_resolver.h'), 'utf8')));
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'ae-queue-contracts-'));
try {
  const binary = path.join(temp, 'queue');
  const compile = spawnSync(process.env.CXX || 'c++', ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', native,
    '-x', 'c++', '-', '-o', binary], { input: '#include "item_injection_queue_test.h"\nint main(){return audit_injection_queue()?0:1;}\n', encoding: 'utf8' });
  assert.equal(compile.status, 0, compile.stderr);
  const run = spawnSync(binary, [], { encoding: 'utf8' });
  assert.equal(run.status, 0, run.stderr);
  const scanner = path.join(temp, 'scanner');
  const scanBuild = spawnSync(process.env.CXX || 'c++', ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', native,
    path.join(root, 'test/byte-scan.cpp'), '-o', scanner], { encoding: 'utf8' });
  assert.equal(scanBuild.status, 0, scanBuild.stderr);
  const scanRun = spawnSync(scanner, [], { encoding: 'utf8' });
  assert.equal(scanRun.status, 0, scanRun.stderr);
} finally { fs.rmSync(temp, { recursive: true, force: true }); }
console.log('Production provenance, queue contracts and 4000 byte-matcher equivalence cases verified.');
