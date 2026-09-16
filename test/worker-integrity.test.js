const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

async function run({ mismatchModule = false, corrupt = '', missingMetadata = false } = {}) {
  const assets = { 'engine.js': Buffer.from('trusted glue'), 'engine.wasm': Buffer.from('trusted wasm') };
  const commit = 'a'.repeat(40);
  const build = { moduleCommit: commit, artifacts: Object.fromEntries(Object.entries(assets)
    .map(([name, data]) => [name, crypto.createHash('sha256').update(data).digest('hex')])) };
  let created = false, message;
  const imports = [];
  const context = {
    crypto: crypto.webcrypto,
    URL, Blob,
    self: { postMessage(value) { message = value; } },
    importScripts(name) { imports.push(name); },
    prepareElfImage: () => ({ 'image.bin': new Uint8Array([1]) }),
    createAENative: async options => {
      created = true;
      assert.deepEqual(Array.from(options.wasmBinary), Array.from(assets['engine.wasm']));
      return { FS: { mkdir() {}, writeFile() {} }, callMain() { options.print('AUDIT_VERSION 2'); return 0; } };
    },
    fetch: async url => {
      const name = url.replace(/^\.\//, '').split('?')[0];
      if (name === 'build.json') return { ok: !missingMetadata, json: async () => build };
      if (name === 'provenance.json') return { ok: true, json: async () => ({ moduleCommit: mismatchModule ? 'b'.repeat(40) : commit, uncommittedSource: false }) };
      const bytes = name === corrupt ? Buffer.from('stale asset') : assets[name];
      return { ok: true, arrayBuffer: async () => Uint8Array.from(bytes).buffer };
    }
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../native/worker.js'), 'utf8'), context);
  await context.self.onmessage({ data: Uint8Array.from([1]).buffer });
  return { created, message, imports };
}

test('worker loads only the engine bytes matching the deployment manifest', async () => {
  const result = await run();
  assert.equal(result.created, true);
  assert.equal(result.message.error, undefined);
  assert.match(result.imports[1], /^blob:/); // Execute the verified bytes, not a second potentially changed fetch.
});
test('mixed module provenance and unavailable metadata fail before engine execution', async () => {
  for (const options of [{ mismatchModule: true }, { missingMetadata: true }]) {
    const result = await run(options);
    assert.equal(result.created, false);
    assert.ok(result.message.error);
  }
});
test('stale JavaScript or WASM assets cannot produce a verification report', async () => {
  for (const corrupt of ['engine.js', 'engine.wasm']) {
    const result = await run({ corrupt });
    assert.equal(result.created, false);
    assert.match(result.message.error, /assets changed/);
    assert.equal(result.message.lines, undefined);
  }
});
