import { createHash } from "node:crypto";
import {
  mkdir,
  readFile,
  rename,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import xxhash from "xxhash-addon";

const { XXHash3 } = xxhash;

const XSH_MAGIC = 0x48534558;
const XSH_VERSION_SWAPPED = 0x19122020;
const TITLE_ID = "415608C3";
const profiles = {
  sp: [
    /^common$/,
    /^code_post_gfx(?:_|$)/,
    /^frontend(?:_|$)/,
    /^patch$/,
  ],
  mp: [
    /^common_mp$/,
    /^code_post_gfx_(?:720_|1080_)?mp$/,
    /^frontend(?:_|$)/,
    /^patch(?:_ui)?_mp$/,
    /^ui_mp$/,
    /^faction_.*_mp$/,
  ],
  zm: [
    /^common_zm$/,
    /^code_post_gfx_(?:720_|1080_)?zm$/,
    /^frontend(?:_|$)/,
    /^patch(?:_ui)?_zm$/,
    /^ui(?:_viewer)?_zm$/,
  ],
};

function usage() {
  console.error(`Usage:
  node scripts/shaders/rexglue-shader-cache.mjs inspect [options]
  node scripts/shaders/rexglue-shader-cache.mjs build --profile <sp|mp|zm> [options]
  node scripts/shaders/rexglue-shader-cache.mjs build --zone <name[,name...]> [options]
  node scripts/shaders/rexglue-shader-cache.mjs build --all [options]

Options:
  --shader-dir <path>   Extractor output (default: shader_work/shaders)
  --output <path>       Output .xsh path
  --merge <path>        Preserve valid entries from an existing .xsh
  --profile <name>      Select boot shaders for sp, mp, or zm
  --zone <names>        Select exact source-zone names, comma-separated
  --all                 Select every extracted shader (large; not recommended)
  --dry-run             Validate and report without writing`);
}

function parseArgs(argv) {
  const options = { command: argv[0] };
  for (let i = 1; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--all" || arg === "--dry-run") {
      options[arg.slice(2).replace("-", "_")] = true;
    } else if (arg.startsWith("--") && i + 1 < argv.length) {
      options[arg.slice(2).replaceAll("-", "_")] = argv[++i];
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  return options;
}

function shaderHash(data) {
  return BigInt(`0x${XXHash3.hash(data).toString("hex")}`);
}

function sha256(data, stage) {
  return createHash("sha256")
    .update(Buffer.from([stage === "vertex" ? 1 : 0]))
    .update(data)
    .digest("hex");
}

function makeHeader() {
  const header = Buffer.alloc(8);
  header.writeUInt32LE(XSH_MAGIC, 0);
  header.writeUInt32LE(XSH_VERSION_SWAPPED, 4);
  return header;
}

function makeEntry(stage, data, hash = shaderHash(data)) {
  const header = Buffer.alloc(12);
  header.writeBigUInt64LE(hash, 0);
  let countAndType = data.length / 4;
  if (stage === "pixel") {
    countAndType |= 0x80000000;
  }
  header.writeUInt32LE(countAndType >>> 0, 8);
  return { hash, stage, data, bytes: Buffer.concat([header, data]) };
}

function parseXsh(data, source) {
  if (
    data.length < 8
    || data.readUInt32LE(0) !== XSH_MAGIC
    || data.readUInt32LE(4) !== XSH_VERSION_SWAPPED
  ) {
    throw new Error(`${source} is not a compatible ReXGlue .xsh file`);
  }

  const entries = [];
  let offset = 8;
  while (offset < data.length) {
    if (offset + 12 > data.length) {
      console.warn(
        `${source}: ignoring a truncated shader header at 0x${offset.toString(16)}`,
      );
      break;
    }
    const storedHash = data.readBigUInt64LE(offset);
    const countAndType = data.readUInt32LE(offset + 8);
    const stage = (countAndType & 0x80000000) !== 0 ? "pixel" : "vertex";
    const byteCount = (countAndType & 0x7fffffff) * 4;
    const end = offset + 12 + byteCount;
    if (end > data.length) {
      console.warn(
        `${source}: ignoring truncated microcode at 0x${offset.toString(16)}`,
      );
      break;
    }
    const microcode = data.subarray(offset + 12, end);
    const actualHash = shaderHash(microcode);
    if (storedHash !== actualHash) {
      console.warn(
        `${source}: ignoring data after an invalid XXH3 hash at 0x${offset.toString(16)}`,
      );
      break;
    }
    entries.push(makeEntry(stage, microcode, storedHash));
    offset = end;
  }
  return entries;
}

function selectedZones(options) {
  if (options.all) {
    return () => true;
  }
  if (options.profile) {
    const expressions = profiles[options.profile];
    if (!expressions) {
      throw new Error(`Unknown profile "${options.profile}"`);
    }
    return (zone) => expressions.some((expression) => expression.test(zone));
  }
  if (options.zone) {
    const zones = new Set(options.zone.split(",").map((zone) => zone.trim()).filter(Boolean));
    return (zone) => zones.has(zone);
  }
  return null;
}

async function loadSelected(shaderDirectory, options) {
  const indexPath = path.join(shaderDirectory, "index.json");
  const index = JSON.parse(await readFile(indexPath, "utf8"));
  const containerByHash = new Map(index.shaders.map((record) => [record.hash, record]));
  const matchesZone = selectedZones(options);
  let selected = matchesZone
    ? index.microcode.filter((record) => record.containers.some((hash) => {
      const container = containerByHash.get(hash);
      return container?.sources.some((source) => matchesZone(source.zone));
    }))
    : index.microcode;
  const entries = [];
  let bytes = 0;
  for (const record of selected) {
    const filePath = path.join(shaderDirectory, "microcode", record.file);
    const data = await readFile(filePath);
    if (data.length !== record.size || data.length === 0 || data.length % 12 !== 0) {
      throw new Error(`${record.file}: invalid size ${data.length} (index: ${record.size})`);
    }
    const actualSha256 = sha256(data, record.stage);
    if (actualSha256 !== record.hash) {
      throw new Error(`${record.file}: SHA-256 mismatch`);
    }
    entries.push(makeEntry(record.stage, data));
    bytes += data.length;
  }
  return { entries, bytes, index };
}

const options = parseArgs(process.argv.slice(2));
if (!["inspect", "build"].includes(options.command)) {
  usage();
  process.exit(1);
}

const projectRoot = path.resolve(import.meta.dirname, "..", "..");
const shaderDirectory = path.resolve(
  projectRoot,
  options.shader_dir ?? path.join("shader_work", "shaders"),
);
const selection = selectedZones(options);
if (options.command === "build" && !selection) {
  throw new Error("build requires --profile, --zone, or --all");
}

const { entries: extractedEntries, bytes, index } = await loadSelected(
  shaderDirectory,
  options.command === "inspect" && !selection ? { ...options, all: true } : options,
);

const entriesByHash = new Map();
let mergedCount = 0;

if (options.merge) {
  const mergePath = path.resolve(options.merge);
  for (const entry of parseXsh(await readFile(mergePath), mergePath)) {
    entriesByHash.set(entry.hash, entry);
    mergedCount++;
  }
}
for (const entry of extractedEntries) {
  entriesByHash.set(entry.hash, entry);
}

console.log(JSON.stringify({
  source_summary: index.summary,
  selected_shaders: extractedEntries.length,
  selected_microcode_bytes: bytes,
  existing_entries_read: mergedCount,
  output_entries: entriesByHash.size,
}, null, 2));

if (options.command === "inspect" || options.dry_run) {
  process.exit(0);
}

if (!options.output) {
  throw new Error("build requires --output");
}
const outputPath = path.resolve(options.output);

await mkdir(path.dirname(outputPath), { recursive: true });
const temporaryPath = `${outputPath}.${process.pid}.tmp`;
const output = Buffer.concat([makeHeader(), ...[...entriesByHash.values()].map((entry) => entry.bytes)]);
await writeFile(temporaryPath, output);
parseXsh(await readFile(temporaryPath), temporaryPath);
await rename(temporaryPath, outputPath);
console.log(`Wrote ${entriesByHash.size} shaders (${output.length} bytes) to ${outputPath}`);
