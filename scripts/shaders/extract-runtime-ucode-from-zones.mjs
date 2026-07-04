// Extracts runtime-exact Xenos ucode programs from decoded zone files.
//
// The 0x102A11xx shader containers in BO2 zones are NOT 4-byte aligned and
// their physicalSize under-counts the program data, so the old container carve
// (extract-shader-containers.mjs) truncated programs and missed unaligned
// occurrences. This extractor scans for container headers at ANY byte offset,
// then locates the real program inside the physical region with a validated
// control-flow walk (see validate-ucode-extent.mjs) and sizes it with the
// empirically proven upload rule (extent + 3 dwords). The XXH3 of the slice
// equals the runtime PM4 payload hash, so the output corpus is keyed exactly
// like the live shader cache.
//
// Usage:
//   node scripts/shaders/extract-runtime-ucode-from-zones.mjs <output-dir> [zoneFilter]
//     [--priority below-normal] [--yield-ms 25] [--start-zone 67] [--max-zones 8]
//     [--start-container 0] [--max-containers 2000] [--time-budget-ms 60000]
//     [--resume]
import fs from 'fs';
import os from 'os';
import path from 'path';
import pkg from 'xxhash-addon';
import { computeUcodeExtent } from './validate-ucode-extent.mjs';
const { XXHash3 } = pkg;

const zonesDir = 'shader_work/fastfile_dat';
const rawArgs = process.argv.slice(2);
const valueOptions = new Set([
  '--priority',
  '--yield-ms',
  '--start-zone',
  '--max-zones',
  '--start-container',
  '--max-containers',
  '--time-budget-ms',
]);
function argValue(name, fallback = undefined) {
  const index = rawArgs.indexOf(name);
  if (index < 0) return fallback;
  if (index + 1 >= rawArgs.length) {
    console.error(`${name} expects a value`);
    process.exit(2);
  }
  return rawArgs[index + 1];
}
const positional = [];
for (let i = 0; i < rawArgs.length; i++) {
  if (rawArgs[i].startsWith('--')) {
    if (valueOptions.has(rawArgs[i])) {
      i++;
    }
    continue;
  }
  positional.push(rawArgs[i]);
}
const [outDir, zoneFilter] = positional;
const priority = argValue('--priority', 'below-normal');
const yieldMs = Number(argValue('--yield-ms', '25'));
let startZone = Number(argValue('--start-zone', '0'));
const maxZones = Number(argValue('--max-zones', '0'));
let startContainer = Number(argValue('--start-container', '0'));
const maxContainers = Number(argValue('--max-containers', '0'));
const timeBudgetMs = Number(argValue('--time-budget-ms', '0'));
const resume = rawArgs.includes('--resume');
if (!outDir) {
  console.error('usage: extract-runtime-ucode-from-zones.mjs <output-dir> [zoneFilter] [--priority below-normal] [--yield-ms 25] [--start-zone 67] [--max-zones 8] [--start-container 0] [--max-containers 2000] [--time-budget-ms 60000] [--resume]');
  process.exit(2);
}
fs.mkdirSync(outDir, { recursive: true });
const indexPath = path.join(outDir, 'index.jsonl');
const progressPath = path.join(outDir, 'extract_progress.json');

function priorityValue(value) {
  switch (value.toLowerCase()) {
    case 'idle':
      return os.constants.priority.PRIORITY_LOW;
    case 'below-normal':
    case 'low':
      return os.constants.priority.PRIORITY_BELOW_NORMAL;
    case 'normal':
      return os.constants.priority.PRIORITY_NORMAL;
    default:
      throw new Error(`unknown --priority ${value}; use idle, below-normal, or normal`);
  }
}

try {
  os.setPriority(process.pid, priorityValue(priority));
} catch (error) {
  process.stderr.write(`warning: could not set node priority: ${error.message}\n`);
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

const hashOf = (buf) => {
  const h = new XXHash3(Buffer.alloc(8));
  h.update(buf);
  return h.digest().readBigUInt64BE(0).toString(16).toUpperCase().padStart(16, '0');
};

function discoverUcode(region) {
  const scanLimit = Math.min(1024, region.length - 12);
  for (let start = 0; start <= scanLimit; start++) {
    const avail = (region.length - start) >> 2;
    if (avail < 3) break;
    const n = Math.min(avail, 16384);
    const dwords = new Array(n);
    for (let i = 0; i < n; i++) dwords[i] = region.readUInt32BE(start + i * 4);
    const extent = computeUcodeExtent(dwords);
    if (extent === null) continue;
    const total = extent + 3;
    if (total > avail) continue;
    return { startByte: start, extentDwords: total };
  }
  return null;
}

const zones = fs.readdirSync(zonesDir)
  .filter(f => f.endsWith('.dat') && (!zoneFilter || f.includes(zoneFilter)))
  .sort();

if (resume && fs.existsSync(progressPath)) {
  try {
    const progress = JSON.parse(fs.readFileSync(progressPath, 'utf8'));
    if (progress.zoneFilter === (zoneFilter ?? null) &&
        Number.isInteger(progress.nextZone) &&
        Number.isInteger(progress.nextContainer)) {
      startZone = progress.nextZone;
      startContainer = progress.nextContainer;
    }
  } catch {
    process.stderr.write(`warning: could not parse ${progressPath}; using CLI start position\n`);
  }
}

function writeProgress(nextZone, nextContainer, currentZoneFile, done = false) {
  fs.writeFileSync(progressPath, JSON.stringify({
    zoneFilter: zoneFilter ?? null,
    zones: zones.length,
    nextZone,
    nextContainer,
    currentZoneFile,
    done,
    existingPrograms,
    newPrograms: programsFound,
    containersSeen,
    containersProcessed,
    discoveryFailed,
    updated: new Date().toISOString(),
  }, null, 2));
}

console.log(`zones: ${zones.length}`);
console.log(`priority=${priority} yield_ms=${yieldMs} start_zone=${startZone} max_zones=${maxZones} start_container=${startContainer} max_containers=${maxContainers} time_budget_ms=${timeBudgetMs} resume=${resume ? 'yes' : 'no'}`);

const signature = Buffer.from([0x10, 0x2a, 0x11]);
const seen = new Set();
for (const file of fs.readdirSync(outDir)) {
  const match = file.match(/^(VS|PS)_0x([0-9A-Fa-f]{16})\.ucode$/);
  if (match) seen.add(match[1].toUpperCase() + match[2].toUpperCase());
}
const indexStream = fs.createWriteStream(indexPath, { flags: 'a' });
let containersSeen = 0, programsFound = 0, discoveryFailed = 0;
let containersProcessed = 0;
let existingPrograms = seen.size;

console.log(`existing output shaders: ${existingPrograms}`);

const endZone = maxZones > 0 ? Math.min(zones.length, startZone + maxZones) : zones.length;
let stopRequested = false;
let stopReason = '';
const startedMs = Date.now();
for (let zi = startZone; zi < endZone; zi++) {
  const z = zones[zi];
  const data = fs.readFileSync(path.join(zonesDir, z));
  let at = 0;
  while ((at = data.indexOf(signature, at)) !== -1) {
    const off = at;
    at++;
    if (off + 36 > data.length) continue;
    const flags = data.readUInt32BE(off);
    if ((flags & 0xFFFFFF00) >>> 0 !== 0x102A1100) continue;
    const virtualSize = data.readUInt32BE(off + 4);
    const physicalSize = data.readUInt32BE(off + 8);
    const shaderOffset = data.readUInt32BE(off + 24);
    if (data.readUInt32BE(off + 28) !== 0 || data.readUInt32BE(off + 32) !== 0) continue;
    if (virtualSize < 36 || virtualSize > 1 << 20 || physicalSize === 0 ||
        physicalSize > 1 << 22) continue;
    if (shaderOffset + 8 > virtualSize || off + virtualSize + 8 > data.length) continue;
    const progOff = data.readUInt32BE(off + shaderOffset);
    const regionStart = off + virtualSize + progOff;
    if (regionStart + 12 > data.length) continue;
    containersSeen++;
    if (containersSeen <= startContainer) {
      continue;
    }
    containersProcessed++;
    if (maxContainers > 0 && containersProcessed > maxContainers) {
      stopRequested = true;
      stopReason = `max_containers=${maxContainers}`;
      break;
    }
    if (timeBudgetMs > 0 && Date.now() - startedMs >= timeBudgetMs) {
      stopRequested = true;
      stopReason = `time_budget_ms=${timeBudgetMs}`;
      break;
    }
    if ((containersProcessed % 1000) === 0) {
      console.log(`  scanned containers=${containersSeen} processed=${containersProcessed} unique=${programsFound} failed=${discoveryFailed}`);
      writeProgress(zi, containersSeen, z);
      if (yieldMs > 0) {
        await sleep(yieldMs);
      }
    }
    const stage = (flags & 1) !== 0 ? 'VS' : 'PS';
    const windowEnd = Math.min(data.length, regionStart + (1 << 18));
    const found = discoverUcode(data.subarray(regionStart, windowEnd));
    if (!found) { discoveryFailed++; continue; }
    const begin = regionStart + found.startByte;
    const slice = data.subarray(begin, begin + found.extentDwords * 4);
    const hash = hashOf(slice);
    const key = stage + hash;
    if (seen.has(key)) continue;
    seen.add(key);
    programsFound++;
    const fileName = `${stage}_0x${hash}.ucode`;
    fs.writeFileSync(path.join(outDir, fileName), slice);
    indexStream.write(JSON.stringify({
      stage, runtime_hash: '0x' + hash, dwords: found.extentDwords,
      zone: z, container_offset: off, ucode_offset: begin, file: fileName,
    }) + '\n');
  }
  console.log(`[${zi + 1}/${zones.length}] ${z}: unique=${programsFound} containers=${containersSeen} processed=${containersProcessed} failed=${discoveryFailed}`);
  if (stopRequested) {
    console.log(`stopping after ${stopReason}; resume with --start-zone ${zi} --start-container ${containersSeen}`);
    writeProgress(zi, containersSeen, z);
    break;
  }
  writeProgress(zi + 1, 0, z, zi + 1 >= zones.length);
  if (yieldMs > 0 && zi + 1 < endZone) {
    await sleep(yieldMs);
  }
}

await new Promise(resolve => indexStream.end(resolve));
if (!stopRequested && endZone >= zones.length) {
  writeProgress(zones.length, 0, null, true);
}
console.log(`\ncontainers=${containersSeen} processed=${containersProcessed} existingPrograms=${existingPrograms} newPrograms=${programsFound} discoveryFailed=${discoveryFailed}`);
