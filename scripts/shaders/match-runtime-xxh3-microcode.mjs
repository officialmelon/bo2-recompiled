import fs from 'fs';
import path from 'path';
import pkg from 'xxhash-addon';

const { XXHash3 } = pkg;

function usage() {
  console.error(
    'usage: node scripts/shaders/match-runtime-xxh3-microcode.mjs ' +
      '--capture <events.jsonl> [--microcode-root <dir>]',
  );
}

function parseArgs(argv) {
  const args = {
    microcodeRoot: 'shader_work/shaders/microcode',
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--capture') {
      args.capture = argv[++i];
    } else if (arg === '--microcode-root') {
      args.microcodeRoot = argv[++i];
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

function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help || !args.capture) {
    usage();
    process.exit(args.help ? 0 : 2);
  }

  const root = process.cwd();
  const capturePath = path.resolve(root, args.capture);
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

  const scan = scanMicrocode(microcodeRoot, targets);
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
}

try {
  main();
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
