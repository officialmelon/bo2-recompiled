// Side-by-side dword comparison of a runtime PM4 shader payload and a
// container's program slice, to determine whether the game patches static
// microcode at load time.
// Usage: node scripts/shaders/compare-runtime-vs-container.mjs <events.jsonl> <hash> <container.bin>
import fs from 'fs';
import readline from 'readline';

const [, , eventsPath, hashArg, containerPath] = process.argv;
let payload;
const rl = readline.createInterface({ input: fs.createReadStream(eventsPath) });
for await (const line of rl) {
  if (!line.includes(hashArg) || !line.includes('"dwords"')) continue;
  const ev = JSON.parse(line);
  if (ev.shader_hash !== hashArg || !ev.dwords) continue;
  payload = ev.dwords.map(d => Number(BigInt(d)));
  break;
}
if (!payload) { console.error('payload not found'); process.exit(1); }

const data = fs.readFileSync(containerPath);
const virtualSize = data.readUInt32BE(4);
const shaderOffset = data.readUInt32BE(24);
const progOff = data.readUInt32BE(shaderOffset);
const progSize = data.readUInt32BE(shaderOffset + 4);
const begin = virtualSize + progOff;
const slice = [];
for (let i = 0; i < progSize / 4; i++) slice.push(data.readUInt32BE(begin + i * 4));

console.log(`payload dwords=${payload.length} container program dwords=${slice.length} (begin=${begin})`);
const n = Math.max(payload.length, slice.length);
let diffs = 0;
for (let i = 0; i < n; i++) {
  const a = payload[i], b = slice[i];
  const same = a === b;
  if (!same) diffs++;
  if (i < 96 || !same) {
    console.log(`[${String(i).padStart(3, '0')}] payload=${a === undefined ? '--------' : '0x' + a.toString(16).toUpperCase().padStart(8, '0')} container=${b === undefined ? '--------' : '0x' + b.toString(16).toUpperCase().padStart(8, '0')}${same ? '' : '   DIFF'}`);
  }
}
console.log(`total diff dwords: ${diffs}/${n}`);
