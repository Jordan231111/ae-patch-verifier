#!/usr/bin/env node
// Runs the same ELF loader, compiled WASM and report parser used by the website.
// Fixtures remain local. Each child process gets a fresh WASM heap and patch state.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { prepareElfImage } = require('../native/elf-image.js');
const { nativeReport } = require('../native/report.js');

async function verify(filename) {
  const input = fs.readFileSync(filename);
  const files = prepareElfImage(input);
  const lines = [];
  const engine = await require('../native/engine.js')({ noInitialRun: true,
    print: line => lines.push(line), printErr: line => lines.push(line) });
  engine.FS.mkdir('/input');
  for (const [name, contents] of Object.entries(files)) engine.FS.writeFile('/input/' + name, contents);
  const exitCode = engine.callMain(['/input']);
  const rows = nativeReport(lines);
  const failures = rows.filter(row => row.status === 'FAIL');
  const result = { version: path.basename(path.dirname(filename)),
    sha256: crypto.createHash('sha256').update(input).digest('hex'), exitCode,
    passes: rows.filter(row => row.status === 'PASS').length,
    runtime: rows.filter(row => row.status === 'RUNTIME').length,
    optional: rows.filter(row => row.status === 'N/A').length, failures,
    injection: rows.filter(row => row.feature.startsWith('Item Injection') && row.status === 'PASS') };
  if (process.env.AE_AUDIT_OUTPUT) {
    fs.mkdirSync(process.env.AE_AUDIT_OUTPUT, { recursive: true });
    fs.writeFileSync(path.join(process.env.AE_AUDIT_OUTPUT, result.version + '.json'), JSON.stringify({ ...result, lines }, null, 2));
  }
  console.log(JSON.stringify(result));
  return exitCode === 0 && failures.length === 0;
}

async function main() {
  const args = process.argv.slice(2);
  if (args[0] === '--single') return verify(args[1]);
  if (!args.length) throw new Error('Usage: node scripts/check-native-fixtures.js /path/to/arm64-v8a [libapp.so ...]');
  const files = args.flatMap(p => fs.statSync(p).isDirectory()
    ? fs.readdirSync(p).map(version => path.join(p, version, 'libapp.so')).filter(p => fs.existsSync(p)) : [p]);
  let passed = true;
  for (const filename of files) {
    const child = spawnSync(process.execPath, [__filename, '--single', filename], { encoding: 'utf8', timeout: 180000, maxBuffer: 8 * 1024 * 1024 });
    process.stdout.write(child.stdout || '');
    if (child.stderr) process.stderr.write(child.stderr);
    if (child.error) process.stderr.write(child.error.message + '\n');
    passed &&= child.status === 0;
  }
  return passed;
}
main().then(ok => { process.exitCode = ok ? 0 : 1; }).catch(error => { console.error(error.message); process.exitCode = 1; });
