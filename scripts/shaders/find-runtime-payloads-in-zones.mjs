// Searches decoded zone .dat files for runtime PM4 shader payload bytes to
// discover how the game stores the exact microcode it uploads (alignment,
// length prefix), independent of the 0x102A11xx container carve.
// Usage: node scripts/shaders/find-runtime-payloads-in-zones.mjs <events.jsonl> [zoneFilter] [maxShaders]
import fs from 'fs';
import path from 'path';
import readline from 'readline';

const zonesDir = 'shader_work/fastfile_dat';
const [, , eventsPath, zoneFilter, maxShadersArg] = process.argv;
const maxShaders = Number(maxShadersArg || 8);

const payloads = new Map();
const rl = readline.createInterface({ input: fs.createReadStream(eventsPath) });
for await (const line of rl) {
  if (!line.includes('"pm4_shader"') || !line.includes('"dwords"')) continue;
  let ev;
  try { ev = JSON.parse(line); } catch { continue; }
  if (!ev.dwords || ev.payload_truncated) continue;
  if (payloads.has(ev.shader_hash)) continue;
  const buf = Buffer.alloc(ev.dwords.length * 4);
  ev.dwords.forEach((d, i) => buf.writeUInt32BE(Number(BigInt(d)), i * 4));
  payloads.set(ev.shader_hash, { bytes: buf, shaderType: ev.shader_type });
  if (payloads.size >= maxShaders) break;
}
console.log(`runtime payloads loaded: ${payloads.size}`);

const zones = fs.readdirSync(zonesDir)
  .filter(f => f.endsWith('.dat') && (!zoneFilter || f.includes(zoneFilter)));
console.log(`zones to scan: ${zones.length}`);

const found = new Set();
const CHUNK = 64 * 1024 * 1024;
for (const z of zones) {
  if (found.size === payloads.size) break;
  const p = path.join(zonesDir, z);
  const size = fs.statSync(p).size;
  const fd = fs.openSync(p, 'r');
  const overlap = 8192;
  for (let pos = 0; pos < size; pos += CHUNK - overlap) {
    const len = Math.min(CHUNK, size - pos);
    const buf = Buffer.alloc(len);
    fs.readSync(fd, buf, 0, len, pos);
    for (const [hash, pay] of payloads) {
      if (found.has(hash)) continue;
      const needle = pay.bytes.subarray(0, Math.min(48, pay.bytes.length));
      let idx = buf.indexOf(needle);
      if (idx < 0) continue;
      const abs = pos + idx;
      // Check full payload match by reading directly.
      const full = Buffer.alloc(pay.bytes.length);
      fs.readSync(fd, full, 0, full.length, abs);
      const fullMatch = full.equals(pay.bytes);
      // Context: 16 bytes before.
      const before = Buffer.alloc(16);
      fs.readSync(fd, before, 0, 16, Math.max(0, abs - 16));
      found.add(hash);
      console.log(`\n${hash} (type=${pay.shaderType} bytes=${pay.bytes.length}) in ${z}`);
      console.log(`  offset=${abs} (0x${abs.toString(16)}) aligned4=${abs % 4 === 0} fullMatch=${fullMatch}`);
      console.log(`  16B before: ${before.toString('hex')}`);
    }
  }
  fs.closeSync(fd);
}
console.log(`\nfound ${found.size}/${payloads.size}`);
