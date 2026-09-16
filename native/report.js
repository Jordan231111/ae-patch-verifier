(function (scope) {
  const coverage = typeof module !== 'undefined' ? require('./coverage.js') : scope.AENativeCoverage;
  const optionalLua = new Set(coverage.optionalTargets);
  const injectionLabels = {
    resolver: 'Complete production grant resolver', token_repository: 'Token repository',
    token_assign: 'Token assignment', token_kind: 'Item token classification', base_change: 'Shared amount change',
    ticket_writer: 'Key / ticket history wrapper', dynamic_cast: 'Game dynamic_cast',
    master_getter: 'Item master getter', type_getter: 'Semantic item type getter',
    resource_gate: 'Resource replenishment predicate', other_resources: 'Additional resource repository',
    sync_manager: 'Save manager consensus', sync: 'Save / synchronization function',
    initial_weapon: 'First initial-equipment predicate', initial_armor: 'Second initial-equipment predicate',
    initial_equipment: 'Third initial-equipment predicate', unknown_factory: 'Unidentified-equipment factory',
    rtti: 'Item and ticket RTTI operands', change_slot: 'Instruction-derived grant virtual slot',
    token_pool: 'Token pool member and low-water mark', other_pool: 'Additional resource field and low-water mark',
    amount_max: 'Instruction-derived inventory ceiling', equipment_master: 'Equipment eligibility member consensus',
    embedded_id: 'Embedded item ID / secure-copy agreement', queue: 'Production input and queue contract tests'
  };
  const runtimeChecks = [
    ['Live item objects', 'Object lifetime, type metadata and actual ABI calls require the running game.'],
    ['Exact grants and special types', 'Real inventory deltas, keys, equipment instances and rollback cannot be established from a .so.'],
    ['Adaptive batching', 'Token supply, instance cost, timing and memory pressure are runtime state; decoded thresholds are checked above.'],
    ['Network and saving', 'Replenishment, reconnection, server acknowledgement and persistence require runtime verification.'],
    ['Android integration', 'GL-thread scheduling, focus, input persistence and existing-feature isolation require the installed module.']
  ];
  function nativeReport(lines) {
    const rows = [];
    const row = (feature, check, count, detail, pass = count === 1) => rows.push({ feature, check, count, detail, status: pass ? 'PASS' : 'FAIL', klass: pass ? 'ok' : 'fail' });
    const targetCounts = new Map(), checkCounts = new Map();
    const absent = new Set(lines.flatMap(line => /^OPTIONAL_ABSENT (\S+)$/.exec(line)?.slice(1) || []));
    row('Verifier coverage', 'Current engine protocol', lines.filter(line => line === 'AUDIT_VERSION 2').length,
      'Requires the engine version that includes Item Injection; stale or incomplete output cannot pass.');
    for (const line of lines) {
      const r = /^RESULT (\S+) (0x[0-9a-f]+)$/i.exec(line);
      if (r) {
        targetCounts.set(r[1], (targetCounts.get(r[1]) || 0) + 1);
        if (r[2] === '0x0' && optionalLua.has(r[1]) && absent.has(r[1])) rows.push({ feature: r[1], check: 'Optional Lua API', count: 0, detail: 'Not present in this game version; the module skips this optional API.', status: 'N/A', klass: 'warn' });
        else {
          const diagnostic = r[2] === '0x0' && r[1].startsWith('lua.')
            ? lines.find(line => line.startsWith('AE_TRACE lua registration ' + r[1].slice(4) + ' skipped ')) : null;
          row(r[1], 'Production resolver: one validated target', r[2] === '0x0' ? 0 : 1,
            diagnostic ? diagnostic.replace('AE_TRACE ', '') : 'RVA ' + r[2]);
        }
      }
      const check = /^CHECK ((?:injection|dialogue)\.\w+) ([01])(?: (.*))?$/.exec(line);
      if (check) {
        checkCounts.set(check[1], (checkCounts.get(check[1]) || 0) + 1);
        row(check[1].startsWith('dialogue.') ? 'Dialogue · static' : check[1] === 'injection.queue' ? 'Item Injection · module logic' : 'Item Injection · static',
          injectionLabels[check[1].slice(10)] || check[1], Number(check[2]), check[3] || '');
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
    for (const target of coverage.targets) if (targetCounts.get(target) !== 1)
      row(target, 'Required resolver coverage', 0, 'Missing or duplicate production resolver output.');
    for (const check of coverage.checks) if (checkCounts.get(check) !== 1)
      row(check.startsWith('dialogue.') ? 'Dialogue · static' : 'Item Injection · static', injectionLabels[check.slice(10)] || check, 0, 'Required check was not emitted exactly once.');
    for (const marker of ['LAYOUT layout_resolved ', 'LAYOUT object_layouts ', 'LAYOUT live_shop_layout '])
      if (!lines.some(line => line.startsWith(marker))) row('Layout coverage', marker.trim(), 0, 'Required production layout report is missing.');
    for (const { feature: name, variants } of coverage.bytePatches) {
      for (const enable of [1, 0]) {
        const escaped = variants.map(value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|');
        const re = new RegExp('AE_TRACE byte patch (?:' + escaped + ') enable=' + enable + ' addr=(0x[0-9a-f]+) ok=1');
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
    const events = lines.flatMap(line => {
      const write = /^WRITE (0x[0-9a-f]+) size=(\d+)$/.exec(line);
      return write ? [{ start: BigInt(write[1]), size: BigInt(write[2]) }] : [];
    });
    const applied = result ? Number(result[2]) : 0, undone = result ? Number(result[3]) : 0;
    const ownedRanges = writes => {
      const sorted = writes.map(w => [w.start, w.start + w.size]).sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0);
      const merged = [];
      for (const [start, end] of sorted) {
        const previous = merged[merged.length - 1];
        if (previous && start <= previous[1]) previous[1] = end > previous[1] ? end : previous[1];
        else merged.push([start, end]);
      }
      return merged.map(([start, end]) => start.toString(16) + ':' + end.toString(16)).join(',');
    };
    const sameOwnedWrites = applied > 0 && undone > 0 && events.every(w => w.size > 0n) &&
      events.length === applied + undone && ownedRanges(events.slice(0, applied)) === ownedRanges(events.slice(applied));
    const exact = Boolean(result && result[1] === '1' && result[4] === '1' && sameOwnedWrites);
    row('Patch safety', 'Complete image restored byte-for-byte', exact ? 1 : 0,
      result ? `${applied} apply writes / ${undone} undo writes; same owned ranges=${sameOwnedWrites}; restored=${result[4]}` : 'Native verification did not complete.');
    if (!rows.some(r => r.feature === 'Item dump')) row('Item dump', 'Catalog resolver', 0, 'The full catalog/name contract did not resolve.');
    for (const [check, detail] of runtimeChecks)
      rows.push({ feature: 'Item Injection · runtime', check, count: '—', detail, status: 'RUNTIME', klass: 'warn' });
    return rows;
  }
  function verifyNative(data) {
    return new Promise((resolve, reject) => {
      const worker = new Worker('./native/worker.js');
      const timer = setTimeout(() => { worker.terminate(); reject(new Error('Native verification timed out')); }, 180000);
      const finish = () => { clearTimeout(timer); worker.terminate(); };
      worker.onerror = event => { finish(); reject(new Error(event.message || 'Native verifier failed')); };
      worker.onmessage = ({ data: result }) => {
        try {
          if (result.error) throw new Error(result.error);
          const rows = nativeReport(result.lines);
          finish();
          resolve(rows);
        } catch (error) {
          finish();
          reject(error);
        }
      };
      const copy = data.slice();
      worker.postMessage(copy.buffer, [copy.buffer]);
    });
  }
  if (typeof document !== 'undefined') {
    fetch('./native/provenance.json', { cache: 'no-store' }).then(response => {
      if (!response.ok) throw new Error('provenance unavailable');
      return response.json();
    }).then(async info => {
      const label = document.getElementById('verifierRevision');
      const build = await fetch('./native/build.json', { cache: 'no-store' })
        .then(response => response.ok ? response.json() : null).catch(() => null);
      if (label) label.textContent = 'ARM64 verifier source: module ' + info.moduleCommit.slice(0, 7) +
        (info.uncommittedSource ? ' (local changes)' : '') + ' · code SHA-256 ' + info.sourceSha256.slice(0, 12) +
        (build?.verifierCommit ? ' · site ' + build.verifierCommit.slice(0, 7) : '');
      if (label && build && build.moduleCommit !== info.moduleCommit)
        label.textContent = 'Verifier asset versions disagree. Reload before relying on these checks.';
    }).catch(() => {
      const label = document.getElementById('verifierRevision');
      if (label) label.textContent = 'Verifier source metadata unavailable';
    });
  }
  scope.verifyNative = verifyNative;
  if (typeof module !== 'undefined') module.exports = { nativeReport };
})(globalThis);
