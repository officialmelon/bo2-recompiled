import { createHash } from "node:crypto";
import { open, mkdir, readdir, writeFile } from "node:fs/promises";
import path from "node:path";

const [inputDirectory, outputDirectory] = process.argv.slice(2);

if (!inputDirectory || !outputDirectory) {
  console.error(
    "Usage: node scripts/shaders/extract-shader-containers.mjs "
      + "<decoded-zone-dir> <output-dir>",
  );
  process.exit(1);
}

const rawDirectory = path.join(outputDirectory, "containers");
const microcodeDirectory = path.join(outputDirectory, "microcode");
const signature = Buffer.from([0x10, 0x2a, 0x11]);
const chunkSize = 32 * 1024 * 1024;

await mkdir(rawDirectory, { recursive: true });
await mkdir(microcodeDirectory, { recursive: true });

const zoneFiles = (await readdir(inputDirectory, { withFileTypes: true }))
  .filter((entry) => entry.isFile() && entry.name.endsWith(".dat"))
  .map((entry) => path.join(inputDirectory, entry.name))
  .sort();

const containers = new Map();
const microcodes = new Map();
let occurrences = 0;
let invalidBounds = 0;

function sha256(data, prefix) {
  const hash = createHash("sha256");
  if (prefix !== undefined) {
    hash.update(Buffer.from([prefix]));
  }
  hash.update(data);
  return hash.digest("hex");
}

async function readAt(file, offset, size) {
  const data = Buffer.allocUnsafe(size);
  const { bytesRead } = await file.read(data, 0, size, offset);
  return bytesRead === size ? data : data.subarray(0, bytesRead);
}

for (let fileIndex = 0; fileIndex < zoneFiles.length; fileIndex++) {
  const zonePath = zoneFiles[fileIndex];
  const zoneName = path.basename(zonePath, ".dat");
  const file = await open(zonePath, "r");
  const { size: fileSize } = await file.stat();
  let position = 0;
  let tail = Buffer.alloc(0);

  while (position < fileSize) {
    const readSize = Math.min(chunkSize, fileSize - position);
    const chunk = await readAt(file, position, readSize);
    const data = tail.length ? Buffer.concat([tail, chunk]) : chunk;
    const baseOffset = position - tail.length;
    let searchOffset = 0;

    while ((searchOffset = data.indexOf(signature, searchOffset)) !== -1) {
      const fileOffset = baseOffset + searchOffset;
      searchOffset++;

      if ((fileOffset & 3) !== 0 || fileOffset + 36 > fileSize) {
        continue;
      }

      const header = searchOffset - 1 + 36 <= data.length
        ? data.subarray(searchOffset - 1, searchOffset - 1 + 36)
        : await readAt(file, fileOffset, 36);
      if (header.length !== 36) {
        continue;
      }

      const flags = header.readUInt32BE(0);
      const virtualSize = header.readUInt32BE(4);
      const physicalSize = header.readUInt32BE(8);
      const containerSize = virtualSize + physicalSize;

      if (
        (flags & 0xffffff00) !== 0x102a1100
        || containerSize < 36
        || fileOffset + containerSize > fileSize
        || header.readUInt32BE(28) !== 0
        || header.readUInt32BE(32) !== 0
      ) {
        continue;
      }

      const container = await readAt(file, fileOffset, containerSize);
      if (container.length !== containerSize) {
        invalidBounds++;
        continue;
      }

      const containerHash = sha256(container);
      const stage = (flags & 1) === 0 ? "pixel" : "vertex";
      let containerRecord = containers.get(containerHash);

      if (!containerRecord) {
        const fileName = `${stage}_${containerHash}.bin`;
        await writeFile(path.join(rawDirectory, fileName), container);
        containerRecord = {
          hash: containerHash,
          stage,
          flags: `0x${flags.toString(16).padStart(8, "0")}`,
          size: containerSize,
          file: fileName,
          sources: [],
        };
        containers.set(containerHash, containerRecord);

        const shaderOffset = container.readUInt32BE(24);
        if (shaderOffset + 8 <= container.length) {
          const physicalOffset = container.readUInt32BE(shaderOffset);
          const microcodeSize = container.readUInt32BE(shaderOffset + 4);
          const microcodeOffset = virtualSize + physicalOffset;

          if (
            microcodeSize > 0
            && microcodeOffset + microcodeSize <= container.length
          ) {
            const microcode = container.subarray(
              microcodeOffset,
              microcodeOffset + microcodeSize,
            );
            const microcodeHash = sha256(microcode, flags & 1);
            let microcodeRecord = microcodes.get(microcodeHash);

            if (!microcodeRecord) {
              const microcodeFile = `${stage}_${microcodeHash}.ucode`;
              await writeFile(
                path.join(microcodeDirectory, microcodeFile),
                microcode,
              );
              microcodeRecord = {
                hash: microcodeHash,
                stage,
                size: microcodeSize,
                file: microcodeFile,
                containers: [],
              };
              microcodes.set(microcodeHash, microcodeRecord);
            }

            microcodeRecord.containers.push(containerHash);
            containerRecord.microcode_hash = microcodeHash;
            containerRecord.microcode_size = microcodeSize;
          } else {
            invalidBounds++;
          }
        } else {
          invalidBounds++;
        }
      }

      containerRecord.sources.push({ zone: zoneName, offset: fileOffset });
      occurrences++;
    }

    tail = data.subarray(Math.max(0, data.length - 2));
    position += readSize;
  }

  await file.close();
  console.log(
    `[${fileIndex + 1}/${zoneFiles.length}] ${zoneName}: `
      + `${containers.size} containers, ${microcodes.size} programs`,
  );
}

const shaderRecords = [...containers.values()].sort(
  (a, b) => a.stage.localeCompare(b.stage) || a.hash.localeCompare(b.hash),
);
const microcodeRecords = [...microcodes.values()].sort(
  (a, b) => a.stage.localeCompare(b.stage) || a.hash.localeCompare(b.hash),
);
const summary = {
  zones: zoneFiles.length,
  occurrences,
  unique_containers: shaderRecords.length,
  pixel_containers: shaderRecords.filter((record) => record.stage === "pixel").length,
  vertex_containers: shaderRecords.filter((record) => record.stage === "vertex").length,
  unique_microcode: microcodeRecords.length,
  pixel_microcode: microcodeRecords.filter((record) => record.stage === "pixel").length,
  vertex_microcode: microcodeRecords.filter((record) => record.stage === "vertex").length,
  container_bytes: shaderRecords.reduce((total, record) => total + record.size, 0),
  microcode_bytes: microcodeRecords.reduce((total, record) => total + record.size, 0),
  invalid_bounds: invalidBounds,
};

await writeFile(
  path.join(outputDirectory, "index.json"),
  JSON.stringify({ summary, shaders: shaderRecords, microcode: microcodeRecords }, null, 2),
);

const csvLines = [
  "hash,stage,flags,container_size,microcode_hash,microcode_size,file,source_count,sources",
  ...shaderRecords.map((record) => [
    record.hash,
    record.stage,
    record.flags,
    record.size,
    record.microcode_hash ?? "",
    record.microcode_size ?? "",
    record.file,
    record.sources.length,
    `"${record.sources
      .map((source) => `${source.zone}@0x${source.offset.toString(16)}`)
      .join(";")}"`,
  ].join(",")),
];
await writeFile(path.join(outputDirectory, "index.csv"), csvLines.join("\r\n"));
console.log(JSON.stringify(summary, null, 2));
