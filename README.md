# Black Ops II Recompiled

> Warning: This game has issues, but is in a playable state.

This repository contains a ReXGlue-based recompiled setup for Call of Duty: Black Ops II.

It currently targets:
- `default` for `default.xex`
- `default_mp` for `default_mp.xex`

This repo does not ship the game, extracted assets, or Xbox 360 executables. You must provide your own legally obtained dump.

## What This Repo Expects

The codegen configs point at:
- `assets/default.xex`
- `assets/default_mp.xex`

The runtime also expects the rest of the game data under `assets/`.

Minimum working layout:

```text
bo2/
  assets/
    default.xex
    default_mp.xex
    ...other extracted BO2 assets...
  default/
  default_mp/
  scripts/
```

`assets/` is intentionally ignored by git and must be supplied locally.

## Required Game Dump

Reference ISO used for this setup:

- File: `Call of Duty - Black Ops II (World).iso`
- SHA-1: `A5F56AD3EC9FE2F1D45A92FB141FD046ED9D5C13`

Use your own dump and verify the hash if you want to match the same source image.

## Required Assets

You need to extract the game contents from your own BO2 dump and place them in `assets/`.

At minimum:
- `assets/default.xex`
- `assets/default_mp.xex`
- the supporting fastfiles / packages / media the game normally loads at runtime

If `default.xex` or `default_mp.xex` are missing, codegen and runtime launch will fail.

## Requirements

Windows:
- CMake 3.25+
- Ninja
- Clang / LLVM
- ReXGlue SDK checked out separately
- `rexglue` available on `PATH`

Linux:
- CMake 3.25+
- Ninja
- Clang 20 / `clang++-20`
- ReXGlue SDK checked out separately
- `rexglue` available on `PATH`

Both projects also expect the SDK to be available via `REXSDK_DIR` or the existing local CMake setup.

## Setup Flow

The normal flow is:

1. Extract your BO2 game files into `assets/`
2. Run codegen
3. Run CMake preset/configure
4. Build

## Supported Platforms

| Platform | Status | Support |
| :--- | :---: | :---: |
| **Windows** | Tested | Supported |
| **Linux** | Untested | Unsupported (temporarily) |

## Windows

Codegen:

```bat
scripts\windows\codegen.bat sp
scripts\windows\codegen.bat mp
scripts\windows\codegen.bat both
```

Preset/configure:

```bat
scripts\windows\preset.bat sp
scripts\windows\preset.bat mp
scripts\windows\preset.bat both
scripts\windows\preset.bat both --preset win-amd64-debug
```

Build:

```bat
scripts\windows\build_and_setup.bat sp
scripts\windows\build_and_setup.bat mp
scripts\windows\build_and_setup.bat both
scripts\windows\build_and_setup.bat both --preset win-amd64-relwithdebinfo
```

Accepted target aliases:
- `sp` or `default`
- `mp` or `default_mp`
- `both`

Accepted preset style:
- `--preset win-amd64-debug`
- or bare preset name such as `win-amd64-debug`

## Linux

Codegen:

```bash
scripts/linux/codegen.sh sp
scripts/linux/codegen.sh mp
scripts/linux/codegen.sh both
```

Preset/configure:

```bash
scripts/linux/preset.sh sp
scripts/linux/preset.sh mp
scripts/linux/preset.sh both
scripts/linux/preset.sh both --preset linux-amd64-debug
```

Build:

```bash
scripts/linux/build_and_setup.sh sp
scripts/linux/build_and_setup.sh mp
scripts/linux/build_and_setup.sh both
scripts/linux/build_and_setup.sh both --preset linux-amd64-relwithdebinfo
```

## Output

Built executables are copied to:

```text
out/
  default.exe
  default_mp.exe
```

On Linux, the build script copies:

```text
out/
  default
  default_mp
```

## Notes

- This repo is source + build logic only. It does not include redistributable game content.
- `assets/` must stay local.
- If you change the supplied XEX files, rerun codegen before rebuilding.
- `default` and `default_mp` each have their own codegen config:
  - [default/default_config.toml](/d:/reglue/bo2/default/default_config.toml)
  - [default_mp/default_mp_config.toml](/d:/reglue/bo2/default_mp/default_mp_config.toml)
