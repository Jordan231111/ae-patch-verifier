const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const coverage = require('../native/coverage.js');

test('unexpected engine output rejects the UI request instead of leaving verification pending', async () => {
  let terminated = false, timerCleared = false;
  class Worker {
    terminate() { terminated = true; }
    postMessage() { queueMicrotask(() => this.onmessage({ data: { lines: null } })); }
  }
  const context = { module: { exports: {} }, require: () => coverage, Worker,
    setTimeout: () => 1, clearTimeout: () => { timerCleared = true; } };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../native/report.js'), 'utf8'), context);
  await assert.rejects(context.verifyNative(new Uint8Array([1])));
  assert.equal(terminated, true);
  assert.equal(timerCleared, true);
});
