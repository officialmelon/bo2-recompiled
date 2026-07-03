#!/usr/bin/env node
// BO2 native renderer regression harness.
//
// Runs replay validation, real D3D12 replay, and the runtime shader
// precompile report against a fixed set of captures, then compares the
// parsed results with scripts/regression/goldens.json.
//
// Usage:
//   node scripts/regression/native-renderer-regression.mjs           # compare
//   node scripts/regression/native-renderer-regression.mjs --update # rewrite goldens
//   node scripts/regression/native-renderer-regression.mjs --case mp080-real
//
// Exit code 0 = all checks match goldens; 1 = drift or execution failure.

import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..", "..");
const goldenPath = path.join(scriptDir, "goldens.json");
const workDir = path.join(repoRoot, "logs", "regression");

const replayExe = path.join(
  repoRoot,
  "default", "out", "build", "win-amd64-clangmsvc-debug",
  "native_render_replay.exe");
const inspectExe = path.join(
  repoRoot,
  "default", "out", "build", "win-amd64-clangmsvc-debug",
  "native_shader_inspect.exe");

const captures = {
  mp028: "native_captures/live_d3d12_mp_028/events.jsonl",
  mp080: "native_captures/live_d3d12_mp_080_shader_probe_write_snapshot/events.jsonl",
  sidecar002: "native_captures/sidecar_capture_002/events.jsonl",
  cap4096: "native_captures/shader_payload_cap4096_001/events.jsonl",
};

// Each case produces a flat result object that is compared with the golden.
const cases = [
  { name: "mp028-validate", kind: "validate", capture: "mp028" },
  { name: "mp080-validate", kind: "validate", capture: "mp080" },
  { name: "sidecar002-validate", kind: "validate", capture: "sidecar002" },
  { name: "cap4096-validate", kind: "validate", capture: "cap4096" },
  { name: "mp028-real", kind: "real-d3d12", capture: "mp028" },
  { name: "mp080-real", kind: "real-d3d12", capture: "mp080" },
  { name: "sidecar002-real", kind: "real-d3d12", capture: "sidecar002" },
  { name: "mp080-precompile", kind: "precompile", capture: "mp080", topShaders: 20 },
  { name: "mp028-precompile", kind: "precompile", capture: "mp028", topShaders: 20 },
  { name: "mp080-xenia-replay", kind: "xenia-replay", capture: "mp080", cacheRoot: "native_captures/tmp_xenia_dxbc_mp080" },
  { name: "mp028-xenia-replay", kind: "xenia-replay", capture: "mp028", cacheRoot: "shader_work/cache" },
  { name: "mp080-dxbc", kind: "precompile-dxbc", capture: "mp080", topShaders: 20 },
  { name: "mp028-dxbc", kind: "precompile-dxbc", capture: "mp028", topShaders: 20 },
  { name: "cap4096-dxbc", kind: "precompile-dxbc", capture: "cap4096", topShaders: 24 },
];

function run(exe, args, label) {
  const result = spawnSync(exe, args, {
    cwd: repoRoot,
    encoding: "utf8",
    maxBuffer: 64 * 1024 * 1024,
    timeout: 10 * 60 * 1000,
  });
  if (result.error) {
    throw new Error(`${label}: failed to launch: ${result.error.message}`);
  }
  return {
    code: result.status,
    stdout: result.stdout ?? "",
    stderr: result.stderr ?? "",
  };
}

function sha256File(filePath) {
  return createHash("sha256").update(readFileSync(filePath)).digest("hex").toUpperCase();
}

function parseValidate(stdout) {
  const match = stdout.match(/Validation OK: (\d+) events, (\d+) frames, (\d+) draws/);
  if (!match) return null;
  return { events: +match[1], frames: +match[2], draws: +match[3] };
}

function parseRealReplay(stdout) {
  const out = {};
  const submitted = stdout.match(
    /D3D12 real (?:frame )?replay submitted (\d+) supported draw\(s\) across (\d+) shader pair\(s\)/);
  if (submitted) {
    out.submitted_draws = +submitted[1];
    out.shader_pairs = +submitted[2];
  }
  const pso = stdout.match(/diagnostic_pipelines=(\d+)/);
  if (pso) out.diagnostic_pipelines = +pso[1];
  const layouts = stdout.match(/input_layout_variants=(\d+)/);
  if (layouts) out.input_layout_variants = +layouts[1];
  const textures = stdout.match(
    /bound (\d+) captured texture SRV\(s\), (\d+) fallback texture SRV\(s\), unsupported_texture_attempts=(\d+)/);
  if (textures) {
    out.captured_texture_srvs = +textures[1];
    out.fallback_texture_srvs = +textures[2];
    out.unsupported_texture_attempts = +textures[3];
  }
  return out;
}

function parsePrecompileDxbc(stdout) {
  const match = stdout.match(
    /attempted=(\d+) translated=(\d+) no_complete_payload=(\d+) translator_failed=(\d+) validation_failed=(\d+)/);
  if (!match) return null;
  return {
    attempted: +match[1],
    translated: +match[2],
    no_complete_payload: +match[3],
    translator_failed: +match[4],
    validation_failed: +match[5],
  };
}

function parsePrecompile(stdout) {
  const match = stdout.match(
    /attempted=(\d+) compiled=(\d+) cache_hits=(\d+) no_payload=(\d+) translator_failed=(\d+) dxc_failed=(\d+)/);
  if (!match) return null;
  return {
    attempted: +match[1],
    compiled: +match[2],
    no_payload: +match[4],
    translator_failed: +match[5],
    dxc_failed: +match[6],
  };
}

function runCase(testCase) {
  const capture = path.join(repoRoot, captures[testCase.capture]);
  if (!existsSync(capture)) {
    return { error: `capture missing: ${captures[testCase.capture]}` };
  }

  if (testCase.kind === "validate") {
    const r = run(replayExe, ["--capture", capture, "--validate", "--no-summary"],
      testCase.name);
    const parsed = parseValidate(r.stdout);
    if (r.code !== 0 || !parsed) {
      return { error: `validate failed (exit ${r.code})`, tail: r.stdout.slice(-400) + r.stderr.slice(-400) };
    }
    return parsed;
  }

  if (testCase.kind === "real-d3d12") {
    const output = path.join(workDir, `${testCase.name}.bmp`);
    const r = run(replayExe, [
      "--capture", capture,
      "--backend", "d3d12",
      "--d3d12-output", output,
      "--d3d12-draws", "4096",
      "--no-summary",
    ], testCase.name);
    if (r.code !== 0) {
      return { error: `real replay failed (exit ${r.code})`, tail: r.stdout.slice(-600) + r.stderr.slice(-600) };
    }
    const parsed = parseRealReplay(r.stdout);
    parsed.output_sha256 = existsSync(output) ? sha256File(output) : "missing";
    return parsed;
  }

  if (testCase.kind === "xenia-replay") {
    const output = path.join(workDir, `${testCase.name}.bmp`);
    const r = run(replayExe, [
      "--capture", capture,
      "--backend", "d3d12-xenia",
      "--shader-cache-root", path.join(repoRoot, testCase.cacheRoot),
      "--d3d12-output", output,
      "--d3d12-draws", "4096",
      "--no-summary",
    ], testCase.name);
    if (r.code !== 0) {
      return { error: `xenia replay failed (exit ${r.code})`, tail: r.stdout.slice(-600) + r.stderr.slice(-600) };
    }
    const parsed = {};
    const submitted = r.stdout.match(
      /Xenia-translated replay submitted (\d+) draw\(s\) across (\d+) shader pair\(s\), captured_texture_srvs=(\d+) fallback_texture_srvs=(\d+)/);
    if (submitted) {
      parsed.submitted_draws = +submitted[1];
      parsed.shader_pairs = +submitted[2];
      parsed.captured_texture_srvs = +submitted[3];
      parsed.fallback_texture_srvs = +submitted[4];
    }
    parsed.output_sha256 = existsSync(output) ? sha256File(output) : "missing";
    return parsed;
  }

  if (testCase.kind === "precompile-dxbc") {
    const cacheRoot = path.join(workDir, `${testCase.name}-cache`);
    rmSync(cacheRoot, { recursive: true, force: true });
    const r = run(inspectExe, [
      "--capture", capture,
      "--precompile-runtime-shaders-dxbc", cacheRoot,
      "--top-shaders", String(testCase.topShaders),
    ], testCase.name);
    const parsed = parsePrecompileDxbc(r.stdout);
    if (!parsed) {
      return { error: `dxbc precompile summary missing (exit ${r.code})`, tail: r.stdout.slice(-600) + r.stderr.slice(-600) };
    }
    return parsed;
  }

  if (testCase.kind === "precompile") {
    const cacheRoot = path.join(workDir, `${testCase.name}-cache`);
    rmSync(cacheRoot, { recursive: true, force: true });
    const reportPath = path.join(workDir, `${testCase.name}-report.jsonl`);
    const r = run(inspectExe, [
      "--capture", capture,
      "--precompile-runtime-shaders-d3d12", cacheRoot,
      "--precompile-runtime-shaders-report", reportPath,
      "--top-shaders", String(testCase.topShaders),
    ], testCase.name);
    const parsed = parsePrecompile(r.stdout);
    if (!parsed) {
      return { error: `precompile summary missing (exit ${r.code})`, tail: r.stdout.slice(-600) + r.stderr.slice(-600) };
    }
    return parsed;
  }

  return { error: `unknown case kind ${testCase.kind}` };
}

function diffObjects(expected, actual) {
  const keys = new Set([...Object.keys(expected ?? {}), ...Object.keys(actual ?? {})]);
  const diffs = [];
  for (const key of keys) {
    const want = expected?.[key];
    const got = actual?.[key];
    if (JSON.stringify(want) !== JSON.stringify(got)) {
      diffs.push(`    ${key}: expected ${JSON.stringify(want)}, got ${JSON.stringify(got)}`);
    }
  }
  return diffs;
}

const args = process.argv.slice(2);
const update = args.includes("--update");
const caseFilterIndex = args.indexOf("--case");
const caseFilter = caseFilterIndex >= 0 ? args[caseFilterIndex + 1] : null;

mkdirSync(workDir, { recursive: true });

if (!existsSync(replayExe)) {
  console.error(`native_render_replay.exe not found at ${replayExe}; build the default target first.`);
  process.exit(1);
}

const goldens = existsSync(goldenPath) ? JSON.parse(readFileSync(goldenPath, "utf8")) : {};
const results = {};
let failures = 0;

for (const testCase of cases) {
  if (caseFilter && testCase.name !== caseFilter) continue;
  process.stdout.write(`[${testCase.name}] running...`);
  const started = Date.now();
  let result;
  try {
    result = runCase(testCase);
  } catch (error) {
    result = { error: error.message };
  }
  const seconds = ((Date.now() - started) / 1000).toFixed(1);
  results[testCase.name] = result;

  if (result.error) {
    failures += 1;
    console.log(` FAIL (${seconds}s)`);
    console.log(`    ${result.error}`);
    if (result.tail) console.log(`    tail: ${result.tail.replaceAll("\n", " | ")}`);
    continue;
  }

  if (update) {
    console.log(` recorded (${seconds}s) ${JSON.stringify(result)}`);
    continue;
  }

  const diffs = diffObjects(goldens[testCase.name], result);
  if (diffs.length > 0) {
    failures += 1;
    console.log(` DRIFT (${seconds}s)`);
    for (const diff of diffs) console.log(diff);
  } else {
    console.log(` ok (${seconds}s)`);
  }
}

if (update) {
  const merged = { ...goldens, ...results };
  writeFileSync(goldenPath, JSON.stringify(merged, null, 2) + "\n");
  console.log(`\nGoldens written to ${path.relative(repoRoot, goldenPath)}`);
  process.exit(failures > 0 ? 1 : 0);
}

console.log(failures === 0 ? "\nAll regression cases match goldens." : `\n${failures} case(s) failed.`);
process.exit(failures > 0 ? 1 : 0);
