const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { nativeReport } = require('../native/report.js');
const coverage = require('../native/coverage.js');
const lines = fs.readFileSync(path.join(__dirname, 'fixtures/native-3.17.0.log'), 'utf8').trim().split('\n');
const failures = rows => rows.filter(row => row.status === 'FAIL');

test('actual 3.17.0 browser-engine output covers every static contract', () => {
  const rows = nativeReport(lines);
  assert.deepEqual(failures(rows), []);
  assert.equal(rows.filter(row => row.feature.startsWith('Item Injection') && row.status === 'PASS').length, coverage.checks.filter(id => id.startsWith('injection.')).length);
  assert.equal(rows.filter(row => row.status === 'RUNTIME').length, 7);
  assert.ok(rows.filter(row => row.status === 'RUNTIME').every(row => row.klass === 'warn'));
});

test('empty, truncated and pre-injection engine output cannot pass', () => {
  assert.ok(failures(nativeReport([])).length > coverage.checks.length);
  const stale = lines.filter(line => !/^(CHECK |AUDIT_VERSION |RESULT lua.changeItemAmount )/.test(line));
  assert.ok(failures(nativeReport(stale)).length >= coverage.checks.length + 1);
  const interrupted = lines.filter(line => !/^(READ_ONLY |AUDIT_COMPLETE )/.test(line));
  assert.equal(failures(nativeReport(interrupted)).length, 2);
});

test('each required grant check must appear once, including optional module capabilities', () => {
  for (const id of coverage.checks) {
    const prefix = 'CHECK ' + id + ' ';
    assert.ok(failures(nativeReport(lines.filter(line => !line.startsWith(prefix)))).length > 0, id);
    assert.ok(failures(nativeReport([...lines, lines.find(line => line.startsWith(prefix))])).length > 0, id);
  }
});

test('a valid zero pool-member offset passes; a failed resolver flag does not', () => {
  const rows = nativeReport(lines);
  assert.ok(rows.some(row => row.check.includes('Token pool member') && row.detail.includes('begin=0') && row.status === 'PASS'));
  const damaged = lines.map(line => line.startsWith('CHECK injection.amount_max ') ? 'CHECK injection.amount_max 0 unresolved' : line);
  assert.equal(failures(nativeReport(damaged)).length, 1);
});

test('missing layout and duplicate target reports fail closed', () => {
  const missing = lines.filter(line => !line.startsWith('LAYOUT object_layouts '));
  assert.ok(failures(nativeReport(missing)).some(row => row.feature === 'Layout coverage'));
  const duplicate = [...lines, 'RESULT lua.changeItemAmount 0x1234'];
  assert.ok(failures(nativeReport(duplicate)).some(row => row.check === 'Required resolver coverage'));
});

test('optional Lua absence is explicitly N/A rather than a static pass', () => {
  const oldVersion = [...lines.map(line => line.startsWith('RESULT lua.helixChangeItemAmount ') ? 'RESULT lua.helixChangeItemAmount 0x0' : line), 'OPTIONAL_ABSENT lua.helixChangeItemAmount'];
  const rows = nativeReport(oldVersion);
  assert.deepEqual(failures(rows), []);
  assert.ok(rows.some(row => row.feature === 'lua.helixChangeItemAmount' && row.status === 'N/A' && row.klass === 'warn'));
});

test('ambiguous optional bindings remain failures, and required targets cannot claim absence', () => {
  const ambiguous = lines.map(line => line.startsWith('RESULT lua.helixChangeItemAmount ') ? 'RESULT lua.helixChangeItemAmount 0x0' : line);
  assert.equal(failures(nativeReport(ambiguous)).length, 1);
  const required = [...lines.map(line => line.startsWith('RESULT lua.changeItemAmount ') ? 'RESULT lua.changeItemAmount 0x0' : line), 'OPTIONAL_ABSENT lua.changeItemAmount'];
  assert.equal(failures(nativeReport(required)).length, 1);
});


test('protocol 3 requires unchanged bytes and complete audit markers exactly once', () => {
  for (const marker of ['READ_ONLY unchanged=1', 'AUDIT_COMPLETE ok=1']) {
    assert.ok(failures(nativeReport(lines.filter(line => line !== marker))).length > 0);
    assert.ok(failures(nativeReport([...lines, marker])).length > 0);
  }
  const changed = lines.map(line => line === 'READ_ONLY unchanged=1' ? 'READ_ONLY unchanged=0' : line);
  assert.ok(failures(nativeReport(changed)).length > 0);
  assert.ok(failures(nativeReport(lines.map(line => line === 'AUDIT_VERSION 3' ? 'AUDIT_VERSION 2' : line))).length > 0);
});

test('every generated target is required exactly once', () => {
  for (const target of coverage.targets) {
    const prefix = 'RESULT ' + target + ' ';
    assert.ok(failures(nativeReport(lines.filter(line => !line.startsWith(prefix)))).length > 0, target);
    const result = lines.find(line => line.startsWith(prefix));
    assert.ok(failures(nativeReport([...lines, result])).length > 0, target);
  }
});

test('successful-looking output cannot override a nonzero engine exit', () => {
  assert.ok(failures(nativeReport(lines, 7)).some(row => row.check === 'Native exit code'));
});
