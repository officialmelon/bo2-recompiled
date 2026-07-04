// Locates runtime PM4 shader payload bytes inside the static shader
// containers to derive where the real ucode program starts relative to the
// container header fields (virtualSize + Shader.physicalOffset).
//
// Usage: node scripts/shaders/find-runtime-payloads-in-containers.mjs <events.jsonl> [maxShaders]
import fs from 'fs';
import path from 'path';
import readline from 'readline';

const containersDir = 'shader_work/shaders/containers';
const [, , eventsPath, maxShadersArg] = process.argv;
if (!eventsPath) {
  console.error('usage: find-runtime-payloads-in-containers.mjs <events.jsonl> [maxShaders]');
  process.exit(2);
}
const maxShaders = Number(maxShadersArg || 12);

const payloads = new Map(); // hash -> {bytes, dwordCount, shaderType}
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

const files = fs.readdirSync(containersDir).filter(f => f.endsWith('.bin'));
console.log(`containers: ${files.length}`);
const found = new Set();
for (const f of files) {
  if (found.size === payloads.size) break;
  const data = fs.readFileSync(path.join(containersDir, f));
  for (const [hash, p] of payloads) {
    if (found.has(hash)) continue;
    // Search with a mid-payload chunk so a differing prologue still matches,
    // then measure how much of the payload aligns around the anchor.
    const anchorOff = Math.min(64, Math.max(0, p.bytes.length - 32)) & ~3;
    const needle = p.bytes.subarray(anchorOff, Math.min(anchorOff + 24, p.bytes.length));
    if (needle.length < 12) continue;
    const idx = data.indexOf(needle);
    if (idx < 0) continue;
    const payloadStartInFile = idx - anchorOff;
    let matchBegin = anchorOff, matchEnd = anchorOff + needle.length;
    while (matchBegin > 0 && payloadStartInFile + matchBegin - 1 >= 0 &&
           data[payloadStartInFile + matchBegin - 1] === p.bytes[matchBegin - 1]) matchBegin--;
    while (matchEnd < p.bytes.length && payloadStartInFile + matchEnd < data.length &&
           data[payloadStartInFile + matchEnd] === p.bytes[matchEnd]) matchEnd++;
    found.add(hash);
    const virtualSize = data.readUInt32BE(4);
    const shaderOffset = data.readUInt32BE(24);
    const progOff = data.readUInt32BE(shaderOffset);
    const progSize = data.readUInt32BE(shaderOffset + 4);
    const expected = virtualSize + progOff;
    console.log(`\n${hash} (type=${p.shaderType} payloadBytes=${p.bytes.length}) in ${f}`);
    console.log(`  match window in payload: [${matchBegin}, ${matchEnd}) of ${p.bytes.length}`);
    console.log(`  payloadStartInFile=${payloadStartInFile} expectedStart=${expected} delta=${payloadStartInFile - expected}`);
    console.log(`  virtualSize=${virtualSize} progOff=0x${progOff.toString(16)} progSize=${progSize} fileSize=${data.length}`);
    console.log(`  physicalOffsetOfPayload=0x${(payloadStartInFile - virtualSize).toString(16)} payloadEnd-progEnd=${(payloadStartInFile + p.bytes.length) - (expected + progSize)}`);
  }
}
console.log(`\nfound ${found.size}/${payloads.size} payloads in containers`);
