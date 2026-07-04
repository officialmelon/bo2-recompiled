// Maps container program-slice XXH3 hashes against runtime hashes from the
// seeded shader cache, and hexdumps the first dwords of matched slices so we
// can compare their structure against the metadata-prefixed crash case.
import fs from 'fs';
import path from 'path';
import pkg from 'xxhash-addon';
const { XXHash3 } = pkg;

const containersDir = 'shader_work/shaders/containers';
const cacheIndex = 'shader_work/cache/shader_cache_index.jsonl';

// Runtime hashes = records WITHOUT source_container (captured/live-translated).
const runtimeHashes = new Set();
for (const line of fs.readFileSync(cacheIndex, 'utf8').split('\n')) {
  if (!line.includes('"runtime_hash"') || line.includes('source_container')) continue;
  const m = line.match(/"runtime_hash":"0x([0-9A-Fa-f]+)"/);
  if (m) runtimeHashes.add(BigInt('0x' + m[1]));
}
console.log(`runtime hashes in cache: ${runtimeHashes.size}`);

const hashSlice = (buf) => {
  const h = new XXHash3(Buffer.alloc(8));
  h.update(buf);
  const d = h.digest();
  return d.readBigUInt64BE(0);
};

let scanned = 0, matched = 0, dumped = 0;
const files = fs.readdirSync(containersDir).filter(f => f.endsWith('.bin'));
for (const f of files) {
  const data = fs.readFileSync(path.join(containersDir, f));
  if (data.length < 0x60) continue;
  const flags = data.readUInt32BE(0);
  if ((flags & 0xFFFFFF00) >>> 0 !== 0x102A1100) continue;
  const virtualSize = data.readUInt32BE(4);
  const shaderOffset = data.readUInt32BE(24);
  if (shaderOffset + 8 > data.length) continue;
  const progOff = data.readUInt32BE(shaderOffset);
  const progSize = data.readUInt32BE(shaderOffset + 4);
  const begin = virtualSize + progOff;
  if (!progSize || progSize % 4 || begin + progSize > data.length) continue;
  scanned++;
  const slice = data.subarray(begin, begin + progSize);
  const h = hashSlice(slice);
  if (runtimeHashes.has(h)) {
    matched++;
    if (dumped < 3) {
      dumped++;
      console.log(`\nMATCH ${f}`);
      console.log(`  hash=0x${h.toString(16).toUpperCase()} begin=${begin} size=${progSize} progOff=0x${progOff.toString(16)}`);
      const n = Math.min(24, progSize / 4);
      let out = '';
      for (let i = 0; i < n; i++) {
        out += '  [' + String(i).padStart(3, '0') + '] 0x' + slice.readUInt32BE(i * 4).toString(16).toUpperCase().padStart(8, '0') + '\n';
      }
      console.log(out);
    }
  }
}
console.log(`scanned=${scanned} matchedRuntimeHashes=${matched}`);
