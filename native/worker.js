/* Each file receives a fresh module instance so patch ownership never crosses files. */
importScripts('./elf-image.js');
async function verifiedAsset(name, digest) {
  if (!/^[a-f0-9]{64}$/.test(digest || '')) throw new Error('Missing verifier build integrity metadata');
  const response = await fetch('./' + name + '?sha256=' + digest, { cache: 'no-store' });
  if (!response.ok) throw new Error('Unable to load ' + name);
  const bytes = await response.arrayBuffer();
  const actual = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
    byte => byte.toString(16).padStart(2, '0')).join('');
  if (actual !== digest) throw new Error('Verifier assets changed during loading; reload and try again');
  return bytes;
}
self.onmessage = async ({ data }) => {
  try {
    const [build, provenance] = await Promise.all(['build.json', 'provenance.json'].map(async name => {
      const response = await fetch('./' + name, { cache: 'no-store' });
      if (!response.ok) throw new Error('Verifier build metadata is unavailable');
      return response.json();
    }));
    if (build.moduleCommit !== provenance.moduleCommit || provenance.uncommittedSource)
      throw new Error('Verifier source versions disagree; reload and try again');
    // Pin the compiled engine to this deployment, including across an alias move.
    const [wasm, javascript] = await Promise.all([verifiedAsset('engine.wasm', build.artifacts['engine.wasm']),
      verifiedAsset('engine.js', build.artifacts['engine.js'])]);
    const script = URL.createObjectURL(new Blob([javascript], { type: 'text/javascript' }));
    try { importScripts(script); } finally { URL.revokeObjectURL(script); }
    const files = prepareElfImage(new Uint8Array(data));
    const lines = [];
    const engine = await createAENative({ noInitialRun: true, wasmBinary: new Uint8Array(wasm),
      print: line => lines.push(line), printErr: line => lines.push(line) });
    engine.FS.mkdir('/input');
    for (const [name, contents] of Object.entries(files)) engine.FS.writeFile('/input/' + name, contents);
    engine.callMain(['/input']);
    self.postMessage({ lines });
  } catch (error) {
    self.postMessage({ error: error.message || String(error) });
  }
};
