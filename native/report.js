(function (scope) {
  const optionalLua = new Set(['lua.setMysteryItemAmount', 'lua.helixChangeItemAmount', 'lua.getShowTalkSkipButtonTime']);
  function nativeReport(lines) {
    const rows = [];
    const row = (feature, check, count, detail, pass = count === 1) => rows.push({ feature, check, count, detail, status: pass ? 'PASS' : 'FAIL', klass: pass ? 'ok' : 'fail' });
    for (const line of lines) {
      const r = /^RESULT (\S+) (0x[0-9a-f]+)$/i.exec(line);
      if (r) {
        if (r[2] === '0x0' && optionalLua.has(r[1])) rows.push({ feature: r[1], check: 'Optional Lua API', count: 0, detail: 'Not present in this game version.', status: 'N/A', klass: 'ok' });
        else row(r[1], 'Production resolver: one validated target', r[2] === '0x0' ? 0 : 1, 'RVA ' + r[2]);
      }
      if (line.startsWith('LAYOUT layout_resolved ')) row('Item dump', 'Instruction-derived catalog and name ABI', 1, line.slice(23));
      if (line.startsWith('LAYOUT object_layouts ')) {
        for (const name of ['secure', 'shop', 'amount', 'achievementMap', 'event']) {
          const value = Number(new RegExp('\\b' + name + '=(\\d+)').exec(line)?.[1] || 0);
          row('Object layout: ' + name, 'Validated instruction contracts', value, line.slice(22));
        }
      }
      if (line.startsWith('LAYOUT live_shop_layout ')) row('Mass Purchase', 'Selected-item getters and refresh agree', Number(/valid=(\d+)/.exec(line)?.[1] || 0), line.slice(24));
    }
    const byteNames = ['battle.mp.cost', 'battle.mp.delta', 'battle.mp.current', 'battle.mp.max', 'damage.x524288', 'dungeon.skip'];
    for (const name of byteNames) {
      for (const enable of [1, 0]) {
        const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
        const re = new RegExp('AE_TRACE byte patch ' + escaped + (name === 'dungeon.skip' ? '(?:\\.v316)?' : '') + ' enable=' + enable + ' addr=(0x[0-9a-f]+) ok=1');
        const hits = new Set(lines.flatMap(line => { const m = re.exec(line); return m ? [m[1]] : []; }));
        row(name, enable ? 'Apply: unique site' : 'Undo: same owned site', hits.size, 'Uses the module matcher; undo restores captured original bytes.');
      }
    }
    const special = [
      ['Team God Mode', /AE_TRACE team god enable=(\d) cave=(0x[0-9a-f]+).*ok=1/],
      ['Ad bypass', /AE_TRACE ad bypass (enable|disable) availability=(0x[0-9a-f]+).*ok=1/],
      ['Speedy', /AE_TRACE speed constant enable=(\d) addr=(0x[0-9a-f]+) ok=1/],
      ['Mass Purchase owned-count patch', /AE_MPTRACE TOKEN_PURCHASE_OWNED_COUNT_PATCH trigger=audit enable=(\d) addr=(0x[0-9a-f]+) ok=1/],
      ['Encounter mode', /AE_TRACE encounter judge patch desired=(\d).*addr=(0x[0-9a-f]+) ok=1/]
    ];
    for (const [name, re] of special) for (const enable of [1, 0]) {
      const hits = new Set(lines.flatMap(line => { const m = re.exec(line); return m && (m[1] === String(enable) || m[1] === (enable ? 'enable' : 'disable')) ? [m[2]] : []; }));
      row(name, enable ? 'Apply: unique site' : 'Undo: same validated site', hits.size, 'Validated using production patch code on a private image.');
    }
    const repeat = lines.some(line => line === 'IDEMPOTENT on=1 off=1 owned=1');
    row('Patch safety', 'Repeated apply / undo make no extra writes', repeat ? 1 : 0, 'Second apply and second undo must both be no-ops.');
    const result = lines.map(line => /^ROUNDTRIP item=(\d+) applyWrites=(\d+) undoWrites=(\d+) restored=(\d+)$/.exec(line)).find(Boolean);
    const exact = Boolean(result && result[1] === '1' && result[2] === '15' && result[3] === '15' && result[4] === '1');
    row('Patch safety', 'Complete image restored byte-for-byte', exact ? 1 : 0, result ? `${result[2]} apply writes / ${result[3]} undo writes; restored=${result[4]}` : 'Native verification did not complete.');
    if (!rows.some(r => r.feature === 'Item dump')) row('Item dump', 'Catalog resolver', 0, 'The full catalog/name contract did not resolve.');
    return rows;
  }
  function verifyNative(data) {
    return new Promise((resolve, reject) => {
      const worker = new Worker('./native/worker.js');
      const timer = setTimeout(() => { worker.terminate(); reject(new Error('Native verification timed out')); }, 180000);
      const finish = () => { clearTimeout(timer); worker.terminate(); };
      worker.onerror = event => { finish(); reject(new Error(event.message || 'Native verifier failed')); };
      worker.onmessage = ({ data: result }) => {
        finish();
        if (result.error) reject(new Error(result.error)); else resolve(nativeReport(result.lines));
      };
      const copy = data.slice();
      worker.postMessage(copy.buffer, [copy.buffer]);
    });
  }
  if (typeof document !== 'undefined') {
    fetch('./native/provenance.json', { cache: 'no-store' }).then(response => {
      if (!response.ok) throw new Error('provenance unavailable');
      return response.json();
    }).then(info => {
      const label = document.getElementById('verifierRevision');
      if (label) label.textContent = 'Verifier source: module ' + info.moduleCommit.slice(0, 7) +
        (info.uncommittedSource ? ' (local changes)' : '') + ' · code SHA-256 ' + info.sourceSha256.slice(0, 12);
    }).catch(() => {
      const label = document.getElementById('verifierRevision');
      if (label) label.textContent = 'Verifier source metadata unavailable';
    });
  }
  scope.verifyNative = verifyNative;
  if (typeof module !== 'undefined') module.exports = { nativeReport };
})(globalThis);
