const test = require('node:test');
const assert = require('node:assert/strict');
const { prepareElfImage } = require('../native/elf-image.js');

function elf(relocation = false) {
  const b = Buffer.alloc(512);
  b.writeUInt32LE(0x464c457f); b[4] = 2; b[5] = 1;
  b.writeUInt16LE(183, 18);
  b.writeBigUInt64LE(64n, 32); b.writeBigUInt64LE(128n, 40);
  b.writeUInt16LE(56, 54); b.writeUInt16LE(1, 56);
  b.writeUInt16LE(64, 58); b.writeUInt16LE(relocation ? 2 : 1, 60);
  b.writeUInt32LE(1, 64); b.writeUInt32LE(5, 68);
  b.writeBigUInt64LE(512n, 96); b.writeBigUInt64LE(512n, 104);
  if (relocation) {
    b.writeUInt32LE(4, 196); b.writeBigUInt64LE(384n, 216);
    b.writeBigUInt64LE(24n, 224); b.writeBigUInt64LE(24n, 248);
    b.writeBigUInt64LE(480n, 384); b.writeBigUInt64LE(1027n, 392); b.writeBigInt64LE(496n, 400);
  }
  return b;
}

test('ELF normalization preserves the input and confines relocations to the private image', () => {
  const data = elf(true), original = Buffer.from(data);
  const output = prepareElfImage(data);
  assert.equal(output['relocs.txt'], '480 496\n');
  assert.equal(output['segments.txt'], '0 512 5\n');
  assert.deepEqual(data, original);
  assert.notEqual(output['image.bin'].buffer, data.buffer);
});
test('wrong architecture, truncated tables and non-executable files are rejected', () => {
  const wrong = elf(); wrong.writeUInt16LE(62, 18);
  assert.throws(() => prepareElfImage(wrong), /ARM64/);
  assert.throws(() => prepareElfImage(elf().subarray(0, 60)), /outside/);
  const noCode = elf(); noCode.writeUInt32LE(4, 68);
  assert.throws(() => prepareElfImage(noCode), /executable/);
});
test('relocation targets and writes outside the image are rejected', () => {
  for (const [at, value] of [[384, 512n], [400, 513n]]) {
    const data = elf(true); data.writeBigUInt64LE(value, at);
    assert.throws(() => prepareElfImage(data), /escapes/);
  }
  const negative = elf(true); negative.writeBigInt64LE(-1n, 400);
  assert.throws(() => prepareElfImage(negative), /escapes/);
});
