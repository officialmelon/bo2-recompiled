import { spawn } from "node:child_process";
import {
  copyFile,
  mkdir,
  readdir,
  stat,
} from "node:fs/promises";
import os from "node:os";
import path from "node:path";

function parseArgs(argv) {
  const options = { app: "default" };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (!arg.startsWith("--") || i + 1 >= argv.length) {
      throw new Error(`Unknown or incomplete argument: ${arg}`);
    }
    options[arg.slice(2).replaceAll("-", "_")] = argv[++i];
  }
  return options;
}

const options = parseArgs(process.argv.slice(2));
if (!["default", "default_mp"].includes(options.app)) {
  throw new Error("--app must be default or default_mp");
}

const projectRoot = path.resolve(import.meta.dirname, "..", "..");
const executable = path.resolve(
  options.exe ?? path.join(projectRoot, "out", `${options.app}.exe`),
);
const gameRoot = path.resolve(options.game_root ?? path.join(projectRoot, "assets"));
const cacheRoot = path.resolve(
  options.cache_root ?? path.join(projectRoot, "shader_cache", options.app),
);
await mkdir(cacheRoot, { recursive: true });

async function fileHasData(filePath) {
  try {
    return (await stat(filePath)).size > 0;
  } catch {
    return false;
  }
}

const shareableRoot = path.join(cacheRoot, "shaders", "shareable");
const projectXsh = path.join(shareableRoot, "415608C3.xsh");
if (!await fileHasData(projectXsh)) {
  const documentRoots = [
    path.join(os.homedir(), "OneDrive", "Documents"),
    path.join(os.homedir(), "Documents"),
  ];
  for (const documents of documentRoots) {
    const source = path.join(
      documents,
      options.app,
      "cache",
      "shaders",
      "shareable",
    );
    try {
      const files = (await readdir(source)).filter(
        (name) => name.startsWith("415608C3.") || name === "415608C3.xsh",
      );
      if (files.length === 0) {
        continue;
      }
      await mkdir(shareableRoot, { recursive: true });
      for (const name of files) {
        await copyFile(path.join(source, name), path.join(shareableRoot, name));
      }
      console.log(`Imported ${files.length} existing cache files from ${source}`);
      break;
    } catch {
      // Try the next Documents location.
    }
  }
}

const args = [
  `--game_data_root=${gameRoot}`,
  `--cache_path=${cacheRoot}`,
];
if (options.mode) {
  args.push(`--mode=${options.mode}`);
}

console.log(`Launching ${options.app}`);
console.log(`  game:  ${gameRoot}`);
console.log(`  cache: ${cacheRoot}`);
console.log(
  "ReXGlue will store the exact Xenos shaders and native D3D12 pipeline "
    + "variants encountered during this run.",
);

const child = spawn(executable, args, {
  cwd: path.dirname(executable),
  stdio: "inherit",
});
child.once("error", (error) => {
  throw error;
});
const [code] = await new Promise((resolve) => {
  child.once("exit", (...result) => resolve(result));
});
process.exitCode = code ?? 1;
