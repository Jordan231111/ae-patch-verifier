const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

function run(body, verify) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'ae-native-publication-'));
  const native = path.join(root, 'native'); fs.mkdirSync(native);
  fs.writeFileSync(path.join(native, 'engine.js'), 'old js');
  fs.writeFileSync(path.join(native, 'engine.wasm'), 'old wasm');
  try {
    const source = 'import sys,pathlib\nsys.path.insert(0,sys.argv[1])\nfrom native_staging import NativeStage\n' +
      'target=pathlib.Path(sys.argv[2]); stage=NativeStage(target)\n' + body;
    const result = spawnSync('python3', ['-c', source, path.resolve(__dirname, '../scripts'), native], { encoding: 'utf8' });
    verify(result, native);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
}

test('a failed generation preserves both previous artifacts', () => {
  run("(stage.directory/'engine.js').write_text('new js')\nraise SystemExit(23)\n", (result, native) => {
    assert.equal(result.status, 23);
    assert.equal(fs.readFileSync(path.join(native, 'engine.js'), 'utf8'), 'old js');
    assert.equal(fs.readFileSync(path.join(native, 'engine.wasm'), 'utf8'), 'old wasm');
  });
});

test('a complete generation exchanges the entire tree', () => {
  run("(stage.directory/'engine.js').write_text('new js')\n(stage.directory/'engine.wasm').write_text('new wasm')\nstage.publish()\n", (result, native) => {
    assert.equal(result.status, 0, result.stderr);
    assert.equal(fs.readFileSync(path.join(native, 'engine.js'), 'utf8'), 'new js');
    assert.equal(fs.readFileSync(path.join(native, 'engine.wasm'), 'utf8'), 'new wasm');
  });
});

test('concurrent user edits prevent publication and remain intact', () => {
  run("(stage.directory/'engine.js').write_text('new js')\n(target/'engine.js').write_text('user edit')\nstage.publish()\n", (result, native) => {
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /refusing to overwrite/);
    assert.equal(fs.readFileSync(path.join(native, 'engine.js'), 'utf8'), 'user edit');
    assert.equal(fs.readFileSync(path.join(native, 'engine.wasm'), 'utf8'), 'old wasm');
  });
});
