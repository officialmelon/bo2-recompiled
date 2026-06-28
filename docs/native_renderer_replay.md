# Native Renderer Replay

Last updated: 2026-06-28

`native_render_replay.exe` is the first offline replay executable for the BO2 native renderer work. It reads the JSONL written by `native_renderer_capture_path`, reconstructs frame/draw/shader/constant state, and reports enough state per draw to drive backend bring-up without booting the game for every iteration.

The tool also has a first D3D12 debug backend. That backend renders an offscreen BMP from replayed draw events using BO2-owned D3D12 commands. It is a diagnostic native output path, not a full BO2 scene renderer yet.

## Build

The tool is built from the `default` CMake project:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 native_render_replay"
```

Verified output:

- `C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe`
- Size: `888320` bytes.
- Last write time: `2026-06-28 16:44:28`.

## Usage

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --summary --shader-usage --top-shaders 20
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --frame 2 --dump-draws --max-draws 24
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12 --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-replay.bmp --d3d12-draws 4096 --no-summary
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-final-smoke.jsonl --validate
```

Options:

- `--summary`: print capture, event, frame, draw, primitive, and source-select counts.
- `--frame <index>`: select a zero-based replay frame for frame summary and draw dump.
- `--dump-draws`: list reconstructed draw state.
- `--draw <index>`: dump one zero-based global draw.
- `--shader-usage --top-shaders <n>`: show shader hash and shader-pair draw usage.
- `--backend null|offline|d3d12`: backend selector. `d3d12` runs the offscreen D3D12 debug renderer.
- `--d3d12-output <path>`: BMP output path for the D3D12 replay backend.
- `--d3d12-draws <count>`: number of replay draw tiles to render; default is `4096`.
- `--validate`: parse/analyze only and return non-zero on parse errors.

## Verified capture results

Input:

`C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl`

Replay result:

- Events: `20000`
- Parse errors: `0`
- Frames: `86`
- Pre-frame events: `19302`
- Draws: `4388`
- Shader loads: `1011`
- Constant uploads: `170`
- Swaps: `85` PM4 swaps, `86` `VdSwap` calls, `86` present snapshots.
- Missing vertex shader draws: `0`
- Missing pixel shader draws: `0`
- Draws before any captured constants: `1209`
- Unique live shader hashes: `8`
- Live shader pairs: `7`

Event counts:

| Event | Count |
|---|---:|
| `capture_start` | 1 |
| `begin_frame` | 86 |
| `end_frame` | 86 |
| `vd_swap` | 86 |
| `present_snapshot` | 86 |
| `draw_packet_candidate` | 1 |
| `pm4_packet` | 8601 |
| `pm4_draw` | 4388 |
| `pm4_shader` | 1011 |
| `pm4_constants` | 170 |
| `pm4_swap` | 85 |
| `render_command` | 5399 |

Draw opcode summary:

| Opcode | Count | Meaning |
|---|---:|---|
| `0x00000022` | 60 | `PM4_DRAW_INDX` indexed draw path |
| `0x00000036` | 4328 | `PM4_DRAW_INDX_2` auto-index/packetized draw path |

Primitive/source summary:

| Field | Value | Count |
|---|---:|---:|
| Primitive type | `1` | 4008 |
| Primitive type | `4` | 60 |
| Primitive type | `8` | 320 |
| Source select | `0` | 60 |
| Source select | `2` | 4328 |

The current frame markers come from `VdSwap`/present hooks, so most command-processor traffic in this capture is pre-frame from the replay tool's perspective. A real backend must not assume all draw work appears between current `begin_frame` and `end_frame`; the CP stream and present packet together define the usable frame boundary.

## D3D12 debug output

Command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12 --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-replay.bmp --d3d12-draws 4096 --no-summary
```

Result:

- Exit code: `0`
- Output: `C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-replay.bmp`
- Size: `3686454` bytes.
- Last write time: `2026-06-28 16:44:48`.
- SHA-256: `D82EED84E69EED945B8DA0FF70D7F97B80FCCB35392D5E1E822809231DA03E37`

The BMP is a `1280x720` offscreen D3D12 render target copied back to disk. The renderer clears the target, then issues D3D12 `ClearRenderTargetView` calls over per-draw rectangles. Color is derived from the reconstructed VS hash, PS hash, primitive type, and source select. This proves a BO2-owned native D3D12 output path driven by replayed capture events, but it does not yet draw BO2 geometry.

## Reconstructed draw examples

First captured draw:

```text
draw[0] seq=16 frame=pre PM4_DRAW_INDX_2 opcode=0x00000036 packet=0xC0003600 packet_ptr=0x1F85F980 indices=1 prim=1 src=2 indexed=no index_base=0x00000000 index_len=0 index_fmt=0 endian=0
  VS=0xB6C9863F710683EC PS=0xA4A965C189287B99 constants_total=0 constants_frame=0 last_constant_seq=0 state= no_constants_yet
```

First frame with a draw, frame index `2` / frame id `3`:

```text
draw[27] seq=176 frame=2/id=3 PM4_DRAW_INDX_2 opcode=0x00000036 packet=0xC0003601 packet_ptr=0x04F9B168 indices=3 prim=8 src=2 indexed=no index_base=0x00000000 index_len=0 index_fmt=0 endian=0
  VS=0x1E6883FCCDE1F688 PS=0xA4A965C189287B99 constants_total=0 constants_frame=0 last_constant_seq=0 state= no_constants_yet
```

## Shader usage from replay

Top live shader hashes:

| Stage | Hash | Draws | Loads | Max dwords |
|---|---|---:|---:|---:|
| PS | `0xA4A965C189287B99` | 4268 | 403 | 9 |
| VS | `0xB6C9863F710683EC` | 4008 | 167 | 24 |
| VS | `0x1E6883FCCDE1F688` | 235 | 236 | 27 |
| PS | `0x246E20EF10E0DDC7` | 100 | 50 | 117 |
| VS | `0xAB1E86137A0240E8` | 85 | 85 | 15 |
| VS | `0x81311AC4B1FBD082` | 50 | 50 | 39 |
| PS | `0xC4ED2979F29C9139` | 20 | 10 | 72 |
| VS | `0x5D918D91043B3ED0` | 10 | 10 | 63 |

Top live shader pairs:

| VS | PS | Draws |
|---|---|---:|
| `0xB6C9863F710683EC` | `0xA4A965C189287B99` | 4008 |
| `0x1E6883FCCDE1F688` | `0xA4A965C189287B99` | 235 |
| `0x81311AC4B1FBD082` | `0x246E20EF10E0DDC7` | 50 |
| `0xAB1E86137A0240E8` | `0x246E20EF10E0DDC7` | 50 |
| `0xAB1E86137A0240E8` | `0xA4A965C189287B99` | 25 |
| `0x5D918D91043B3ED0` | `0xC4ED2979F29C9139` | 10 |
| `0xAB1E86137A0240E8` | `0xC4ED2979F29C9139` | 10 |

## Current replay logs

Generated logs:

- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-replay-summary.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-replay-frame0.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-replay-frame2.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-replay-draw0.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-replay-smoke-validate.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-replay.out.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-replay.err.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-replay.bmp`

The smoke capture validation result was:

```text
Validation OK: 100 events, 2 frames, 24 draws
```
