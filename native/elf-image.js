/* Normalize an ARM64 ELF into a private image for the production C++ resolver.
 * No uploaded game code is executed. Only defined, in-image relocations are applied.
 */
(function (scope) {
  function prepareElfImage(input) {
    const data = input instanceof Uint8Array ? input : new Uint8Array(input);
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const bound = (offset, length) => {
      if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) || offset < 0 || length < 0 || offset + length > data.length)
        throw new Error('ELF structure is outside the file');
    };
    const u16 = o => { bound(o, 2); return view.getUint16(o, true); };
    const u32 = o => { bound(o, 4); return view.getUint32(o, true); };
    const u64 = o => { bound(o, 8); const n = Number(view.getBigUint64(o, true)); if (!Number.isSafeInteger(n)) throw new Error('ELF integer exceeds the supported range'); return n; };
    bound(0, 64);
    if (u32(0) !== 0x464c457f || data[4] !== 2 || data[5] !== 1 || u16(18) !== 183)
      throw new Error('Expected a little-endian ARM64 ELF');
    const phoff = u64(32), phsize = u16(54), phcount = u16(56);
    if (phsize < 56 || phcount > 1024) throw new Error('Invalid program-header table');
    bound(phoff, phsize * phcount);
    const segments = [];
    let extent = 0;
    for (let i = 0; i < phcount; i++) {
      const at = phoff + i * phsize;
      if (u32(at) !== 1) continue;
      const offset = u64(at + 8), address = u64(at + 16), fileSize = u64(at + 32), size = u64(at + 40), flags = u32(at + 4);
      bound(offset, fileSize);
      if (fileSize > size || address + size > 512 * 1024 * 1024) throw new Error('Unsupported ELF image size');
      segments.push({ offset, address, fileSize, size, flags });
      extent = Math.max(extent, address + size);
    }
    if (!extent || !segments.some(s => s.flags & 1)) throw new Error('ELF has no executable load segment');
    const image = new Uint8Array(extent);
    for (const s of segments) image.set(data.subarray(s.offset, s.offset + s.fileSize), s.address);
    const shoff = u64(40), shsize = u16(58), shcount = u16(60);
    if (shsize < 64 || !shcount || shcount > 8192) throw new Error('ELF section table is required');
    bound(shoff, shsize * shcount);
    const sections = [];
    for (let i = 0; i < shcount; i++) {
      const at = shoff + i * shsize;
      const s = { type: u32(at + 4), offset: u64(at + 24), size: u64(at + 32), link: u32(at + 40), entry: u64(at + 56) };
      if (s.type !== 8) bound(s.offset, s.size);
      sections.push(s);
    }
    const symbol = (table, index) => {
      if (!table || ![2, 11].includes(table.type) || table.entry < 24 || index >= table.size / table.entry)
        throw new Error('Invalid ELF symbol reference');
      const at = table.offset + index * table.entry;
      return { name: u32(at), address: u64(at + 8), section: u16(at + 6) };
    };
    const decoder = new TextDecoder();
    const nameAt = (table, offset) => {
      if (!table || offset >= table.size) throw new Error('Invalid ELF string reference');
      const start = table.offset + offset, end = data.indexOf(0, start);
      if (end < 0 || end >= table.offset + table.size) throw new Error('Unterminated ELF string');
      return decoder.decode(data.subarray(start, end));
    };
    const symbols = [], relocations = [];
    for (const s of sections) {
      if (s.type === 11) {
        if (s.entry < 24 || s.size % s.entry) throw new Error('Invalid dynamic-symbol table');
        for (let i = 0; i < s.size / s.entry; i++) {
          const sym = symbol(s, i);
          if (sym.section && sym.address <= extent && sym.name) {
            const name = nameAt(sections[s.link], sym.name);
            if (/^[^\s]+$/.test(name)) symbols.push(`${name} ${sym.address}\n`);
          }
        }
      }
      if (s.type !== 4) continue;
      if (s.entry < 24 || s.size % s.entry) throw new Error('Invalid relocation table');
      for (let i = 0; i < s.size / s.entry; i++) {
        const at = s.offset + i * s.entry, offset = u64(at);
        const info = view.getBigUint64(at + 8, true), type = Number(info & 0xffffffffn);
        let value = Number(view.getBigInt64(at + 16, true));
        if (type === 1027) {
          // R_AARCH64_RELATIVE
        } else if ([257, 1025, 1026].includes(type)) {
          const sym = symbol(sections[s.link], Number(info >> 32n));
          if (!sym.section) continue;
          value += sym.address;
        } else continue;
        if (!Number.isSafeInteger(value) || value < 0 || value > extent || offset + 8 > extent)
          throw new Error('Relocation escapes the ELF image');
        relocations.push(`${offset} ${value}\n`);
      }
    }
    return {
      'image.bin': image,
      'segments.txt': segments.map(s => `${s.address} ${s.size} ${s.flags}\n`).join(''),
      'relocs.txt': relocations.join(''),
      'symbols.txt': symbols.join('')
    };
  }
  scope.prepareElfImage = prepareElfImage;
  if (typeof module !== 'undefined') module.exports = { prepareElfImage };
})(globalThis);
