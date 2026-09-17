(function (scope) {
  const coverage = typeof module !== 'undefined' ? require('./coverage.js') : scope.AENativeCoverage;
  const optionalLua = new Set(coverage.optionalTargets);
  const injectionLabels = {
    resolver: 'Complete production inventory resolver', token_repository: 'Token repository',
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
    embedded_id: 'Embedded item ID / secure-copy agreement', queue: 'Production input and queue contract tests',
    character_ready_slot: 'Character repository readiness slot', currency_writer: 'Native currency writer',
    currency_exclusion: 'Native currency restriction', equipment_instances: 'Equipment instance operations',
    pet_instances: 'Pet equipment instance operations', buddy_instances: 'Buddy equipment instance operations',
    unknown_instances: 'Unidentified equipment operations', pet_storage: 'Pet authoritative storage',
    pet_survivors: 'Pet survivor preservation contracts', character_bss_bounds: 'Anonymous BSS readiness slot bounds',
    pet_creation_transaction: 'Pet creation publication and rollback contracts',
    fish_pool: 'Fish native pool / size / signature contracts', fish_storage: 'Fish inventory storage contracts',
    lottery_semantics: 'Scalar versus expiry-record ticket semantics', growth_semantics: 'Character growth gift semantics'
  };
  const runtimeDetails = {
    'Live objects and native ABI calls': 'File contracts do not establish the lifetime of a running game object.',
    'Actual additions/removals and instance identity': 'Counts, surviving owners, equipped slots and paired restoration are device checks.',
    'Resource supply and memory pressure': 'Cooperative budgets, cancellation, seeds and token replenishment need the running host.',
    'Save acknowledgement and persistence': 'Client saving and any server-owned transactions require runtime verification.',
    'Android lifecycle and feature isolation': 'JNI, GL dispatch, Activity lifecycle and other modules require an installed build.',
    'ShadowHook install/disable/unhook': 'Framework calling chains, concurrent callbacks and restored behavior cannot run inside this static WASM audit.',
    'Shop ownership and restoration': 'A live StateManager owner and its destruction must be observed on the game thread.'
  };
  const runtimeChecks = coverage.runtimeChecks.map(name => [name, runtimeDetails[name]]);
  function nativeReport(lines, exitCode = 0) {
    if (!Array.isArray(lines) || lines.some(line => typeof line !== 'string')) throw new Error('Invalid native verifier output');
    const rows = [];
    const row = (feature, check, count, detail, pass = count === 1) => rows.push({ feature, check, count, detail, status: pass ? 'PASS' : 'FAIL', klass: pass ? 'ok' : 'fail' });
    if (exitCode !== 0) row('Static audit integrity', 'Native exit code', 0, 'Engine exited with code ' + exitCode);
    const targetCounts = new Map(), checkCounts = new Map();
    const absent = new Set(lines.flatMap(line => /^OPTIONAL_ABSENT (\S+)$/.exec(line)?.slice(1) || []));
    row('Verifier coverage', 'Current engine protocol', lines.filter(line => line === 'AUDIT_VERSION 3').length,
      'Requires the current resolver-only protocol; old byte-patch simulations cannot pass.');
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
      const check = /^CHECK ((?:injection|dialogue|runtime|mass|director|models)\.\w+) ([01])(?: (.*))?$/.exec(line);
      if (check) {
        checkCounts.set(check[1], (checkCounts.get(check[1]) || 0) + 1);
        row(check[1].startsWith('injection.') ? (check[1] === 'injection.queue' ? 'Item Injection · module logic' : 'Item Injection · static') : check[1].split('.')[0] + ' · static',
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
      row(check.startsWith('injection.') ? 'Item Injection · static' : check.split('.')[0] + ' · static', injectionLabels[check.slice(10)] || check, 0, 'Required check was not emitted exactly once.');
    for (const marker of ['LAYOUT layout_resolved ', 'LAYOUT object_layouts ', 'LAYOUT live_shop_layout '])
      if (lines.filter(line => line.startsWith(marker)).length !== 1) row('Layout coverage', marker.trim(), 0, 'Required production layout report is missing or duplicated.');
    const readOnly = lines.filter(line => line === 'READ_ONLY unchanged=1').length;
    row('Static audit integrity', 'Uploaded image remains byte-identical', readOnly,
      'Resolvers and synthetic models run locally; uploaded ARM64 instructions are never executed.');
    row('Static audit integrity', 'Engine completed every audit group',
      lines.filter(line => line === 'AUDIT_COMPLETE ok=1').length, 'Interrupted or failed native checks cannot pass.');
    if (lines.some(line => /^(WRITE |ROUNDTRIP |IDEMPOTENT )/.test(line)))
      row('Static audit integrity', 'Unexpected legacy mutation output', 0, 'The current engine performs no apply/undo byte writes.');
    if (!rows.some(r => r.feature === 'Item dump')) row('Item dump', 'Catalog resolver', 0, 'The full catalog/name contract did not resolve.');
    for (const [check, detail] of runtimeChecks)
      rows.push({ feature: 'Module · runtime', check, count: '—', detail, status: 'RUNTIME', klass: 'warn' });
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
          const rows = nativeReport(result.lines, Number.isInteger(result.exitCode) ? result.exitCode : -1);
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
