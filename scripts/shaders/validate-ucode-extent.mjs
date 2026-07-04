// Validates that a Xenos control-flow walk over a shader program computes the
// exact dword count the game uploads (the PM4 payload size). If this holds for
// every captured runtime payload, the same walk can size programs inside the
// static containers where no explicit size field is known.
// Usage: node scripts/shaders/validate-ucode-extent.mjs <events.jsonl>...
import fs from 'fs';
import readline from 'readline';

// Computes the program extent in dwords from ucode dwords (host-order values
// of the big-endian stream). Returns null if the walk goes out of bounds or
// looks malformed. CF instructions are 48 bits, packed two per 3 dwords;
// opcode is the top nibble; exec address/count are in 3-dword instruction
// slots from program start.
export function computeUcodeExtent(dwords) {
  const kExecOps = new Set([1, 2, 3, 4, 5, 6, 13, 14]);
  const kEndOps = new Set([2, 4, 6, 14]);
  let maxClauseEnd = 0;               // in instruction slots
  let minClauseAddr = Infinity;       // clauses must follow the CF region
  let maxCfSlot = 0;                  // highest CF slot that must exist
  let sawEnd = false;
  let sawExec = false;
  let sawAlloc = false;
  let slot = 0;
  for (; slot < 4096; slot++) {
    const base = (slot >> 1) * 3;
    if (base + 3 > dwords.length) return null;
    let lo, hi;
    if ((slot & 1) === 0) {
      lo = dwords[base];
      hi = dwords[base + 1] & 0xFFFF;
    } else {
      lo = ((dwords[base + 1] >>> 16) | (dwords[base + 2] << 16)) >>> 0;
      hi = dwords[base + 2] >>> 16;
    }
    const opcode = (hi >>> 12) & 0xF;
    if (opcode === 0) {
      // Real NOP CF slots are all zero; anything else is garbage.
      if (lo !== 0 || (hi & 0x0FFF) !== 0) return null;
    }
    if (kExecOps.has(opcode)) {
      sawExec = true;
      const address = lo & 0xFFF;
      const count = (lo >>> 12) & 0x7;
      if (address + count > maxClauseEnd) maxClauseEnd = address + count;
      if (address < minClauseAddr) minClauseAddr = address;
    } else if (opcode === 12) {
      sawAlloc = true;
    } else if (opcode === 7 || opcode === 8 || opcode === 9 || opcode === 11) {
      // loop start/end, cond call, cond jmp: address targets a CF slot.
      const address = lo & 0x1FFF;
      if (address > maxCfSlot) maxCfSlot = address;
    }
    if (kEndOps.has(opcode)) {
      sawEnd = true;
      if (slot >= maxCfSlot) break;
    }
  }
  if (!sawEnd || !sawExec || !sawAlloc) return null;
  const cfEndDwords = ((slot >> 1) + 1) * 3;
  // Structural validity: every referenced clause must start at or after the
  // end of the control-flow region, jump targets must stay inside it, and the
  // clause window must be sane.
  if (minClauseAddr !== Infinity && minClauseAddr * 3 < cfEndDwords) return null;
  // Clauses immediately follow the CF region in compiler output; a large gap
  // means we walked garbage.
  if (minClauseAddr !== Infinity && minClauseAddr * 3 > cfEndDwords + 3) return null;
  if (maxCfSlot > slot) return null;
  if (maxClauseEnd > 4096) return null;
  const extent = Math.max(cfEndDwords, maxClauseEnd * 3);
  return extent;
}

const isMain = import.meta.url === `file://${process.argv[1].replace(/\\/g, '/')}` ||
  process.argv[1].endsWith('validate-ucode-extent.mjs');
if (isMain) {
  const files = process.argv.slice(2);
  let ok = 0, mismatch = 0, failed = 0;
  const seen = new Set();
  for (const f of files) {
    const rl = readline.createInterface({ input: fs.createReadStream(f) });
    for await (const line of rl) {
      if (!line.includes('"pm4_shader"') || !line.includes('"dwords"')) continue;
      let ev;
      try { ev = JSON.parse(line); } catch { continue; }
      if (!ev.dwords || ev.payload_truncated || seen.has(ev.shader_hash)) continue;
      seen.add(ev.shader_hash);
      const dwords = ev.dwords.map(d => Number(BigInt(d)));
      const extent = computeUcodeExtent(dwords);
      const actual = dwords.length;
      if (extent === actual) { ok++; continue; }
      // Trailing padding? Check whether extra dwords past extent are zero.
      const pad = extent !== null && dwords.slice(extent).every(d => d === 0);
      if (extent === null) failed++;
      else mismatch++;
      console.log(`${ev.shader_hash} type=${ev.shader_type} actual=${actual} computed=${extent} zeroTail=${pad}`);
    }
  }
  console.log(`\nok=${ok} mismatch=${mismatch} walkFailed=${failed} total=${seen.size}`);
}
