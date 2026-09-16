/* Each file receives a fresh module instance so patch ownership never crosses files. */
importScripts('./elf-image.js', './engine.js');
self.onmessage = async ({ data }) => {
  try {
    const files = prepareElfImage(new Uint8Array(data));
    const lines = [];
    const engine = await createAENative({ noInitialRun: true, print: line => lines.push(line), printErr: line => lines.push(line) });
    engine.FS.mkdir('/input');
    for (const [name, contents] of Object.entries(files)) engine.FS.writeFile('/input/' + name, contents);
    engine.callMain(['/input']);
    self.postMessage({ lines });
  } catch (error) {
    self.postMessage({ error: error.message || String(error) });
  }
};
