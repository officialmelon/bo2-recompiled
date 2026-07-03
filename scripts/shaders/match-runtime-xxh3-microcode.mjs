import fs from 'fs';
import path from 'path';
import pkg from 'xxhash-addon';

const { XXHash3 } = pkg;

function usage() {
  console.error(
    'usage: node scripts/shaders/match-runtime-xxh3-microcode.mjs ' +
      '--capture <events.jsonl> [--microcode-root <dir>] ' +
      '[--container-root <dir>] [--skip-hash-scan] [--write-map <path>]',
  );
}

function parseArgs(argv) {
  const args = {
    containerRoot: 'shader_work/shaders/containers',
    microcodeRoot: 'shader_work/shaders/microcode',
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--capture') {
      args.capture = argv[++i];
    } else if (arg === '--container-root') {
      args.containerRoot = argv[++i];
    } else if (arg === '--microcode-root') {
      args.microcodeRoot = argv[++i];
    } else if (arg === '--skip-hash-scan') {
      args.skipHashScan = true;
    } else if (arg === '--write-map') {
      args.writeMap = argv[++i];
    } else if (arg === '--help' || arg === '-h') {
      args.help = true;
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }
  return args;
}

function hashHex(buffer) {
  return XXHash3.hash(buffer).toString('hex').toUpperCase();
}

function swappedBe32ToHostBytes(buffer) {
  if (buffer.length % 4 !== 0) {
    return null;
  }
  const swapped = Buffer.allocUnsafe(buffer.length);
  for (let offset = 0; offset < buffer.length; offset += 4) {
    swapped.writeUInt32LE(buffer.readUInt32BE(offset), offset);
  }
  return swapped;
}

function trimTrailingZeroBeDwords(buffer) {
  let size = buffer.length;
  while (size >= 4 && buffer.readUInt32BE(size - 4) === 0) {
    size -= 4;
  }
  return size === buffer.length ? buffer : buffer.subarray(0, size);
}

function trimTrailingZeroLeDwords(buffer) {
  let size = buffer.length;
  while (size >= 4 && buffer.readUInt32LE(size - 4) === 0) {
    size -= 4;
  }
  return size === buffer.length ? buffer : buffer.subarray(0, size);
}

function parseRuntimeShaders(capturePath) {
  const shaders = new Map();
  const lines = fs.readFileSync(capturePath, 'utf8').split(/\r?\n/);
  for (const line of lines) {
    if (!line.includes('"type":"pm4_shader"')) {
      continue;
    }

    const hashMatch = line.match(/"shader_hash":"0x([0-9A-Fa-f]+)"/);
    const stageMatch = line.match(/"shader_type":(\d+)/);
    const dwordsMatch = line.match(/"dwords":\[(.*?)\]/);
    if (!hashMatch || !stageMatch) {
      continue;
    }

    const hash = hashMatch[1].toUpperCase().padStart(16, '0');
    let shader = shaders.get(hash);
    if (!shader) {
      shader = {
        hash,
        stage: Number(stageMatch[1]),
        loads: 0,
        firstDwords: [],
      };
      shaders.set(hash, shader);
    }
    shader.loads++;

    if (shader.firstDwords.length === 0 && dwordsMatch) {
      shader.firstDwords = Array.from(
        dwordsMatch[1].matchAll(/"0x([0-9A-Fa-f]+)"/g),
        (match) => Number.parseInt(match[1], 16) >>> 0,
      );
    }
  }
  return shaders;
}

function hashRuntimePayload(shader) {
  if (shader.firstDwords.length === 0) {
    return {};
  }
  const be = Buffer.allocUnsafe(shader.firstDwords.length * 4);
  const host = Buffer.allocUnsafe(shader.firstDwords.length * 4);
  for (let i = 0; i < shader.firstDwords.length; i++) {
    be.writeUInt32BE(shader.firstDwords[i], i * 4);
    host.writeUInt32LE(shader.firstDwords[i], i * 4);
  }

  return {
    payloadDwords: shader.firstDwords.length,
    beBytes: be,
    hostBytes: host,
    be: hashHex(be),
    host: hashHex(host),
    trimmedBe: hashHex(trimTrailingZeroBeDwords(be)),
    trimmedHost: hashHex(trimTrailingZeroLeDwords(host)),
  };
}

function addMatch(matches, targets, hash, variant, file, size) {
  if (!targets.has(hash)) {
    return;
  }
  matches.push({
    hash,
    variant,
    file,
    size,
  });
}

function scanMicrocode(microcodeRoot, targets) {
  const matches = [];
  let files = 0;
  let variants = 0;
  for (const entry of fs.readdirSync(microcodeRoot, { withFileTypes: true })) {
    if (!entry.isFile() || !entry.name.endsWith('.ucode')) {
      continue;
    }

    const file = path.join(microcodeRoot, entry.name);
    const bytes = fs.readFileSync(file);
    files++;

    variants++;
    addMatch(matches, targets, hashHex(bytes), 'raw_file_bytes', file, bytes.length);

    const trimmedRaw = trimTrailingZeroBeDwords(bytes);
    if (trimmedRaw.length !== bytes.length) {
      variants++;
      addMatch(
        matches,
        targets,
        hashHex(trimmedRaw),
        'raw_file_bytes_trimmed_zero_dwords',
        file,
        bytes.length,
      );
    }

    const swapped = swappedBe32ToHostBytes(bytes);
    if (swapped) {
      variants++;
      addMatch(
        matches,
        targets,
        hashHex(swapped),
        'be32_to_host_words',
        file,
        bytes.length,
      );

      const trimmedSwapped = trimTrailingZeroLeDwords(swapped);
      if (trimmedSwapped.length !== swapped.length) {
        variants++;
        addMatch(
          matches,
          targets,
          hashHex(trimmedSwapped),
          'be32_to_host_words_trimmed_zero_dwords',
          file,
          bytes.length,
        );
      }
    }
  }

  return { files, variants, matches };
}

function payloadNeedles(shaders) {
  const needles = [];
  for (const shader of shaders.values()) {
    const payload = hashRuntimePayload(shader);
    if (!payload.beBytes || payload.beBytes.length === 0) {
      continue;
    }
    needles.push({
      hash: shader.hash,
      stage: shader.stage,
      loads: shader.loads,
      variant: 'runtime_payload_be_bytes',
      bytes: payload.beBytes,
    });
  }
  return needles;
}

function scanExactPayloadSubranges(scanRoot, extension, shaders) {
  const needles = payloadNeedles(shaders);
  const matches = [];
  let files = 0;
  let bytesScanned = 0;
  if (!fs.existsSync(scanRoot)) {
    return { files, bytesScanned, matches };
  }

  for (const entry of fs.readdirSync(scanRoot, { withFileTypes: true })) {
    if (!entry.isFile() || !entry.name.endsWith(extension)) {
      continue;
    }

    const file = path.join(scanRoot, entry.name);
    const bytes = fs.readFileSync(file);
    files++;
    bytesScanned += bytes.length;

    for (const needle of needles) {
      const prefixLength = Math.min(16, needle.bytes.length);
      const prefix = needle.bytes.subarray(0, prefixLength);
      let offset = bytes.indexOf(prefix);
      while (offset !== -1) {
        if (
          offset + needle.bytes.length <= bytes.length &&
          bytes.compare(
            needle.bytes,
            0,
            needle.bytes.length,
            offset,
            offset + needle.bytes.length,
          ) === 0
        ) {
          matches.push({
            hash: needle.hash,
            stage: needle.stage,
            loads: needle.loads,
            variant: needle.variant,
            file,
            offset,
            payloadBytes: needle.bytes.length,
            fileBytes: bytes.length,
          });
        }
        offset = bytes.indexOf(prefix, offset + 1);
      }
    }
  }

  return { files, bytesScanned, matches };
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help || !args.capture) {
    usage();
    process.exit(args.help ? 0 : 2);
  }

  const root = process.cwd();
  const capturePath = path.resolve(root, args.capture);
  const containerRoot = path.resolve(root, args.containerRoot);
  const microcodeRoot = path.resolve(root, args.microcodeRoot);
  const shaders = parseRuntimeShaders(capturePath);
  const targets = new Set(shaders.keys());

  let payloadSelfMatches = 0;
  console.log(`runtime_targets=${shaders.size}`);
  for (const shader of shaders.values()) {
    const payload = hashRuntimePayload(shader);
    const selfMatch =
      payload.be === shader.hash ||
      payload.host === shader.hash ||
      payload.trimmedBe === shader.hash ||
      payload.trimmedHost === shader.hash;
    if (selfMatch) {
      payloadSelfMatches++;
    }
    console.log(
      `target 0x${shader.hash} stage=${shader.stage} loads=${shader.loads}` +
        ` payload_dwords=${payload.payloadDwords ?? 0}` +
        ` payload_xxh3_self=${selfMatch ? 'yes' : 'no'}` +
        ` payload_be=${payload.be ?? 'none'}` +
        ` payload_host=${payload.host ?? 'none'}`,
    );
  }

  const scan = args.skipHashScan
    ? { files: 0, variants: 0, matches: [] }
    : scanMicrocode(microcodeRoot, targets);
  const microcodeSubranges = scanExactPayloadSubranges(
    microcodeRoot,
    '.ucode',
    shaders,
  );
  const containerSubranges = scanExactPayloadSubranges(
    containerRoot,
    '.bin',
    shaders,
  );
  console.log(
    `ucode_files=${scan.files} variants_checked=${scan.variants}` +
      ` runtime_payload_self_matches=${payloadSelfMatches}/${shaders.size}` +
      ` static_ucode_matches=${scan.matches.length}/${shaders.size}`,
  );
  for (const match of scan.matches) {
    console.log(
      `match 0x${match.hash} ${match.variant} ` +
        `${path.relative(root, match.file)} bytes=${match.size}`,
    );
  }
  console.log(
    `ucode_exact_subrange_files=${microcodeSubranges.files}` +
      ` bytes=${microcodeSubranges.bytesScanned}` +
      ` matches=${microcodeSubranges.matches.length}`,
  );
  for (const match of microcodeSubranges.matches) {
    console.log(
      `subrange 0x${match.hash} ${match.variant} ` +
        `${path.relative(root, match.file)} offset=${match.offset}` +
        ` payload_bytes=${match.payloadBytes} file_bytes=${match.fileBytes}`,
    );
  }
  console.log(
    `container_exact_subrange_files=${containerSubranges.files}` +
      ` bytes=${containerSubranges.bytesScanned}` +
      ` matches=${containerSubranges.matches.length}`,
  );
  for (const match of containerSubranges.matches) {
    console.log(
      `container_subrange 0x${match.hash} ${match.variant} ` +
        `${path.relative(root, match.file)} offset=${match.offset}` +
        ` payload_bytes=${match.payloadBytes} file_bytes=${match.fileBytes}`,
    );
  }

  if (args.writeMap) {
    const mapPath = path.resolve(root, args.writeMap);
    fs.mkdirSync(path.dirname(mapPath), { recursive: true });
    const payloads = {};
    for (const shader of shaders.values()) {
      const payload = hashRuntimePayload(shader);
      payloads[shader.hash] = {
        stage: shader.stage,
        loads: shader.loads,
        payload_dwords: payload.payloadDwords ?? 0,
        payload_xxh3_be: payload.be ?? null,
        payload_xxh3_host: payload.host ?? null,
        payload_xxh3_self:
          payload.be === shader.hash ||
          payload.host === shader.hash ||
          payload.trimmedBe === shader.hash ||
          payload.trimmedHost === shader.hash,
      };
    }
    const serializeMatch = (match) => ({
      runtime_hash: `0x${match.hash}`,
      stage: match.stage,
      loads: match.loads,
      variant: match.variant,
      file: path.relative(root, match.file),
      byte_offset: match.offset,
      payload_bytes: match.payloadBytes,
      file_bytes: match.fileBytes,
    });
    const map = {
      schema: 'bo2_runtime_shader_xxh3_static_subranges.v1',
      capture: path.relative(root, capturePath),
      microcode_root: path.relative(root, microcodeRoot),
      container_root: path.relative(root, containerRoot),
      runtime_shader_count: shaders.size,
      payloads,
      static_whole_file_hash_matches: scan.matches.map((match) => ({
        runtime_hash: `0x${match.hash}`,
        variant: match.variant,
        file: path.relative(root, match.file),
        file_bytes: match.size,
      })),
      microcode_subrange_matches:
        microcodeSubranges.matches.map(serializeMatch),
      container_subrange_matches:
        containerSubranges.matches.map(serializeMatch),
    };
    fs.writeFileSync(mapPath, `${JSON.stringify(map, null, 2)}\n`);
    console.log(`wrote_map=${path.relative(root, mapPath)}`);
  }
}

try {
  main();
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
