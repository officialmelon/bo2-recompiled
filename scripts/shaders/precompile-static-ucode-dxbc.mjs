// Robust static .ucode -> Xenia DXBC precompile driver.
//
// native_shader_inspect's in-process translator can stall on individual static
// shaders. This driver feeds it small temporary batches and recursively splits
// timed-out batches down to a single shader, so one bad shader is recorded and
// skipped rather than wedging the full corpus.
//
// Usage:
//   node scripts/shaders/precompile-static-ucode-dxbc.mjs \
//     --tool default/out/build/win-amd64-clangmsvc-debug/native_shader_inspect.exe \
//     --ucode-dir shader_work/shaders/ucode_runtime \
//     --cache-root shader_work/cache-static-ucode \
//     --batch-size 16 \
//     --timeout-ms 30000 \
//     --max-skips 64 \
//     --priority below-normal \
//     --batch-delay-ms 250 \
//     --time-budget-ms 60000

import fs from 'fs';
import os from 'os';
import path from 'path';
import { spawn } from 'child_process';

function argValue(name, fallback = undefined) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  if (index + 1 >= process.argv.length) {
    console.error(`${name} expects a value`);
    process.exit(2);
  }
  return process.argv[index + 1];
}

const repoRoot = process.cwd();
const tool = path.resolve(repoRoot, argValue(
  '--tool',
  'default/out/build/win-amd64-clangmsvc-debug/native_shader_inspect.exe'));
const ucodeDir = path.resolve(repoRoot, argValue(
  '--ucode-dir', 'shader_work/shaders/ucode_runtime'));
const cacheRoot = path.resolve(repoRoot, argValue(
  '--cache-root', 'shader_work/cache-static-ucode'));
const batchSize = Number(argValue('--batch-size', '16'));
const timeoutMs = Number(argValue('--timeout-ms', '30000'));
const limit = Number(argValue('--limit', '0'));
const maxSkips = Number(argValue('--max-skips', '64'));
const priority = argValue('--priority', 'below-normal');
const batchDelayMs = Number(argValue('--batch-delay-ms', '250'));
const timeBudgetMs = Number(argValue('--time-budget-ms', '0'));
const quiet = process.argv.includes('--quiet');

if (!fs.existsSync(tool)) throw new Error(`tool not found: ${tool}`);
if (!fs.existsSync(ucodeDir)) throw new Error(`ucode dir not found: ${ucodeDir}`);
fs.mkdirSync(cacheRoot, { recursive: true });

const skippedPath = path.join(cacheRoot, 'static_precompile_skipped.jsonl');
const progressPath = path.join(cacheRoot, 'static_precompile_progress.json');

function loadCachedHashes() {
  const result = new Set();
  const indexPath = path.join(cacheRoot, 'shader_cache_index.jsonl');
  if (!fs.existsSync(indexPath)) return result;
  const text = fs.readFileSync(indexPath, 'utf8');
  for (const line of text.split(/\r?\n/)) {
    const match = line.match(/"runtime_hash":"(0x[0-9A-Fa-f]+)"/);
    if (match) result.add(match[1].toUpperCase());
  }
  return result;
}

function loadSkippedFiles() {
  const result = new Set();
  if (!fs.existsSync(skippedPath)) return result;
  const text = fs.readFileSync(skippedPath, 'utf8');
  for (const line of text.split(/\r?\n/)) {
    if (!line.trim()) continue;
    try {
      const record = JSON.parse(line);
      if (record.file) result.add(record.file);
    } catch {
      // Keep going if a previous run was interrupted mid-write.
    }
  }
  return result;
}

function hashFromFileName(name) {
  const match = name.match(/^(?:VS|PS)_(0x[0-9A-Fa-f]{16})\.ucode$/);
  return match ? match[1].toUpperCase() : null;
}

function nodePriorityValue(value) {
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

function windowsPriorityClass(value) {
  switch (value.toLowerCase()) {
    case 'idle':
      return 'Idle';
    case 'below-normal':
    case 'low':
      return 'BelowNormal';
    case 'normal':
      return 'Normal';
    default:
      throw new Error(`unknown --priority ${value}; use idle, below-normal, or normal`);
  }
}

function applyCurrentPriority() {
  try {
    os.setPriority(process.pid, nodePriorityValue(priority));
  } catch (error) {
    process.stderr.write(`warning: could not set node priority: ${error.message}\n`);
  }
}

function applyChildPriority(pid) {
  if (process.platform !== 'win32' || priority.toLowerCase() === 'normal') return;
  const priorityClass = windowsPriorityClass(priority);
  const command = `$p = Get-Process -Id ${pid} -ErrorAction SilentlyContinue; if ($p) { $p.PriorityClass = '${priorityClass}' }`;
  const setter = spawn('powershell.exe', [
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-Command', command,
  ], {
    windowsHide: true,
    stdio: 'ignore',
  });
  setter.on('error', () => {});
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function runTool(batchDir) {
  return new Promise((resolve) => {
    const child = spawn(tool, [
      '--precompile-static-shaders-dxbc', cacheRoot,
      '--ucode-dir', batchDir,
    ], {
      cwd: repoRoot,
      windowsHide: true,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    applyChildPriority(child.pid);
    let stdout = '';
    let stderr = '';
    const timer = setTimeout(() => {
      child.kill('SIGKILL');
    }, timeoutMs);
    child.stdout.on('data', data => { stdout += data.toString(); });
    child.stderr.on('data', data => { stderr += data.toString(); });
    child.on('close', (code, signal) => {
      clearTimeout(timer);
      resolve({ code, signal, stdout, stderr, timedOut: signal === 'SIGKILL' });
    });
  });
}

function makeBatchDir(files) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'bo2-ucode-batch-'));
  for (const file of files) {
    fs.copyFileSync(path.join(ucodeDir, file), path.join(dir, file));
  }
  return dir;
}

function removeDirQuiet(dir) {
  try {
    fs.rmSync(dir, { recursive: true, force: true });
  } catch {
    // Best-effort temp cleanup.
  }
}

function appendSkipped(files, reason, detail) {
  const now = new Date().toISOString();
  for (const file of files) {
    fs.appendFileSync(skippedPath, JSON.stringify({
      time: now,
      file,
      reason,
      detail: detail.slice(0, 1000),
    }) + '\n');
  }
}

async function precompileBatch(files, depth = 0) {
  if (files.length === 0) return { ok: 0, skipped: 0 };
  const batchDir = makeBatchDir(files);
  const result = await runTool(batchDir);
  removeDirQuiet(batchDir);
  if (result.code === 0 && !result.timedOut) {
    if (!quiet) {
      process.stdout.write(
        `ok batch=${files.length} first=${files[0]} last=${files[files.length - 1]}\n`);
    }
    return { ok: files.length, skipped: 0 };
  }
  if (files.length > 1) {
    const mid = Math.floor(files.length / 2);
    const left = await precompileBatch(files.slice(0, mid), depth + 1);
    const right = await precompileBatch(files.slice(mid), depth + 1);
    return { ok: left.ok + right.ok, skipped: left.skipped + right.skipped };
  }
  const reason = result.timedOut ? 'timeout' : `exit_${result.code ?? result.signal}`;
  appendSkipped(files, reason, `${result.stdout}\n${result.stderr}`);
  process.stdout.write(`skip ${files[0]} reason=${reason}\n`);
  return { ok: 0, skipped: 1 };
}

const allFiles = fs.readdirSync(ucodeDir)
  .filter(name => hashFromFileName(name))
  .sort();
const cached = loadCachedHashes();
const skippedFiles = loadSkippedFiles();
let pending = allFiles.filter(name =>
  !cached.has(hashFromFileName(name)) && !skippedFiles.has(name));
if (limit > 0) pending = pending.slice(0, limit);

applyCurrentPriority();
console.log(`tool=${tool}`);
console.log(`ucode_dir=${ucodeDir}`);
console.log(`cache_root=${cacheRoot}`);
console.log(
  `total=${allFiles.length} cached=${cached.size} skipped=${skippedFiles.size} pending=${pending.length}`);
console.log(`batch_size=${batchSize} timeout_ms=${timeoutMs} max_skips=${maxSkips}`);
console.log(`priority=${priority} batch_delay_ms=${batchDelayMs} time_budget_ms=${timeBudgetMs}`);

let ok = 0;
let skipped = 0;
const startedMs = Date.now();
for (let i = 0; i < pending.length; i += batchSize) {
  if (timeBudgetMs > 0 && Date.now() - startedMs >= timeBudgetMs) {
    console.log(`stopping after time_budget_ms=${timeBudgetMs}; resume later`);
    break;
  }
  const batch = pending.slice(i, i + batchSize);
  const result = await precompileBatch(batch);
  ok += result.ok;
  skipped += result.skipped;
  fs.writeFileSync(progressPath, JSON.stringify({
    total: allFiles.length,
    pending: pending.length,
    processed: Math.min(i + batchSize, pending.length),
    ok,
    skipped,
    updated: new Date().toISOString(),
  }, null, 2));
  if (maxSkips > 0 && skipped >= maxSkips) {
    console.log(`stopping after ${skipped} skips; resume later or raise --max-skips`);
    break;
  }
  if (batchDelayMs > 0 && i + batchSize < pending.length) {
    await sleep(batchDelayMs);
  }
}

console.log(`done ok=${ok} skipped=${skipped}`);
