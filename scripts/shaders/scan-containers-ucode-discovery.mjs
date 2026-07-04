// Discovers the real (byte-unaligned) ucode program inside each static shader
// container: scans byte offsets in the physical program region, runs a Xenos
// control-flow extent walk at each candidate, sizes the program as extent + 3
// dwords (empirical upload rule validated against 20/20 runtime payloads), and
// checks the XXH3 of the big-endian slice against runtime hashes from the
// shader cache.
// Usage: node scripts/shaders/scan-containers-ucode-discovery.mjs [maxFiles]
import fs from 'fs';
import path from 'path';
import pkg from 'xxhash-addon';
import { computeUcodeExtent } from './validate-ucode-extent.mjs';
const { XXHash3 } = pkg;

const containersDir = 'shader_work/shaders/containers';
const cacheIndex = 'shader_work/cache/shader_cache_index.jsonl';
const maxFiles = Number(process.argv[2] || 0);

const runtimeHashes = new Set();
for (const line of fs.readFileSync(cacheIndex, 'utf8').split('\n')) {
  if (!line.includes('"runtime_hash"') || line.includes('source_container')) continue;
  const m = line.match(/"runtime_hash":"0x([0-9A-Fa-f]+)"/);
  if (m) runtimeHashes.add('0x' + m[1].toUpperCase());
}
console.log(`runtime hashes: ${runtimeHashes.size}`);

const hashOf = (buf) => {
  const h = new XXHash3(Buffer.alloc(8));
  h.update(buf);
  return '0x' + h.digest().readBigUInt64BE(0).toString(16).toUpperCase().padStart(16, '0');
};

// Finds the ucode program inside the physical region. Returns
// {startByte, extentDwords} or null.
export function discoverUcode(region) {
  const scanLimit = Math.min(1024, region.length - 12);
  for (let start = 0; start <= scanLimit; start++) {
    const avail = (region.length - start) >> 2;
    if (avail < 3) break;
    const dwords = new Array(Math.min(avail, 16384));
    for (let i = 0; i < dwords.length; i++) dwords[i] = region.readUInt32BE(start + i * 4);
    const extent = computeUcodeExtent(dwords);
    if (extent === null) continue;
    const total = extent + 3;
    if (total > avail) continue;
    return { startByte: start, extentDwords: total };
  }
  return null;
}

const isMain = process.argv[1] && process.argv[1].endsWith('scan-containers-ucode-discovery.mjs');
if (isMain) {
  let files = fs.readdirSync(containersDir).filter(f => f.endsWith('.bin'));
  if (maxFiles) files = files.slice(0, maxFiles);
  let discovered = 0, noProgram = 0, matches = 0;
  const matchedHashes = new Set();
  for (const f of files) {
    const data = fs.readFileSync(path.join(containersDir, f));
    if (data.length < 0x60) continue;
    const virtualSize = data.readUInt32BE(4);
    const shaderOffset = data.readUInt32BE(24);
    if (shaderOffset + 8 > data.length) continue;
    const progOff = data.readUInt32BE(shaderOffset);
    const begin = virtualSize + progOff;
    if (begin + 12 > data.length) { noProgram++; continue; }
    const region = data.subarray(begin);
    const found = discoverUcode(region);
    if (!found) { noProgram++; continue; }
    discovered++;
    const slice = region.subarray(found.startByte, found.startByte + found.extentDwords * 4);
    const h = hashOf(slice);
    if (runtimeHashes.has(h)) {
      matches++;
      matchedHashes.add(h);
      console.log(`MATCH ${h} in ${f} startByte=${found.startByte} dwords=${found.extentDwords}`);
    }
  }
  console.log(`\nfiles=${files.length} discovered=${discovered} noProgram=${noProgram}`);
  console.log(`runtime-hash matches: ${matchedHashes.size}/${runtimeHashes.size}`);
}
