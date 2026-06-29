# Native Renderer Replay

Last updated: 2026-06-28

`native_render_replay.exe` is the first offline replay executable for the BO2 native renderer work. It reads the JSONL written by `native_renderer_capture_path`, reconstructs frame/draw/shader/constant state, and reports enough state per draw to drive backend bring-up without booting the game for every iteration.

The tool also has D3D12 replay backends. `d3d12-diagnostic` renders an offscreen BMP from replayed draw events using BO2-owned D3D12 commands, including a small HLSL shader pipeline and synthetic triangle draws. `d3d12` now renders the first supported captured indexed draw with real replayed vertex/index data and a diagnostic native shader. Neither path is a full BO2 scene renderer yet.

## Build

The tool is built from the `default` CMake project:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 native_render_replay"
```

Verified output:

- `C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe`
- Size: `951808` bytes.
- Last write time: `2026-06-28 22:33:38`.

## Usage

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --summary --shader-usage --top-shaders 20
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --frame 2 --dump-draws --max-draws 24
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --draw 1209 --dump-bound-state --dump-constants --dump-indices --dump-vertices --resource-summary --no-summary
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12-diagnostic --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-updated.bmp --d3d12-draws 4096 --no-summary
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-final-smoke.jsonl --validate
```

Options:

- `--summary`: print capture, event, frame, draw, primitive, and source-select counts.
- `--frame <index>`: select a zero-based replay frame for frame summary and draw dump.
- `--dump-draws`: list reconstructed draw state.
- `--draw <index>`: dump one zero-based global draw.
- `--dump-bound-state`: list the selected draw's bound shader hashes and known constant ranges.
- `--dump-constants`: list captured constant uploads; with `--draw`, lists constants known before that draw.
- `--dump-indices`: report indexed-draw metadata, captured raw index bytes, and decoded index values when an index snapshot is available.
- `--dump-vertices`: report fetch/vertex state coverage for the selected draw, including decoded vertex component previews for known Xenos formats.
- `--resource-summary`: report current replay resource snapshot coverage.
- `--shader-usage --top-shaders <n>`: show shader hash and shader-pair draw usage.
- `--backend null|offline|d3d12-diagnostic|d3d12|vulkan-diagnostic|vulkan`: backend selector. `d3d12-diagnostic` runs the offscreen D3D12 debug renderer. `d3d12` runs the current resource-backed captured-geometry path when a supported indexed draw is available. `vulkan*` fails closed until a Vulkan backend exists.
- `--d3d12-output <path>`: BMP output path for the D3D12 replay backend.
- `--d3d12-draws <count>`: number of replay draw tiles to render; default is `4096`.
- `--validate`: parse/analyze only and return non-zero on parse errors or strict replay validation errors such as indexed draws with missing index snapshots.

## Verified index-payload capture

Input:

`C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\events.jsonl`

Replay result:

- Events: `25000`
- Parse errors: `0`
- Frames: `106`
- Pre-frame events: `24144`
- Draws: `5471`
- Shader loads: `1276`
- Constant uploads: `227`, all with payload
- Indexed draw snapshots: `81`, all with payload
- Index payload bytes: `972`
- Missing indexed snapshots: `0`
- Validation: `Validation OK: 25000 events, 106 frames, 5471 draws`

First indexed draw with raw index bytes, draw index `1209`:

```text
draw[1209] seq=5335 frame=pre PM4_DRAW_INDX opcode=0x00000022 packet=0xC0032201 packet_ptr=0x0501B6E0 indices=6 prim=4 src=0 indexed=yes index_base=0x0501E090 index_len=12 index_fmt=0 endian=1 payload=12/12 bytes
  VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 constants_total=2 constants_frame=2 last_constant_seq=5330 state= ok
```

Index dump:

```text
raw index bytes: 0x00,0x03,0x00,0x00,0x00,0x02,0x00,0x02,0x00,0x00,0x00,0x01
decoded_indices=6/6: 3,0,2,2,0,1
```

D3D12 diagnostic replay on the same capture:

- Output: `C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp`
- Exit code: `0`
- Size: `3686454` bytes
- SHA-256: `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`

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
- Constant uploads with payload in the existing capture: `0`
- Constant uploads missing payload in the existing capture: `170`

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

## D3D12 diagnostic output

Command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_renderer_direct_build\native_render_replay_direct.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12-diagnostic --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-updated.bmp --d3d12-draws 4096 --no-summary
```

Result:

- Exit code: `0`
- Output: `C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-updated.bmp`
- Size: `3686454` bytes.
- Last write time: `2026-06-28 20:07:57`.
- SHA-256: `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`

The BMP is a `1280x720` offscreen D3D12 render target copied back to disk. The renderer clears the target, issues D3D12 `ClearRenderTargetView` calls over per-draw rectangles, then uses a root-signature/PSO path with runtime-compiled HLSL and `DrawInstanced` calls to overlay synthetic triangle rectangles. Color is derived from the reconstructed VS hash, PS hash, primitive type, and source select. This proves a BO2-owned native D3D12 output path driven by replayed capture events, but it does not yet draw BO2 geometry.

Latest verified vertex-fetch capture:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\vertex_fetch_capture_001\events.jsonl --validate
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\vertex_fetch_capture_001\events.jsonl --draw 1004 --dump-bound-state --dump-indices --dump-vertices --no-summary
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\vertex_fetch_capture_001\events.jsonl --resource-summary --no-summary
```

Results:

- Validation: `Validation OK: 12000 events, 53 frames, 2646 draws`
- Resource summary: `31` indexed snapshots, `222` vertex fetch records, `222` vertex-buffer snapshots, `16892` vertex payload bytes
- Draw `1004`: decoded indices `3,0,2,2,0,1`, `vf95` at `0x05008230`, stride `32`, Xenos formats `38`, `6`, `37`, and `128/128` vertex bytes
- Draw `1004` decoded vertex preview: positions `(0,0,0,1)`, `(1280,0,0,1)`, `(1280,720,0,1)`, `(0,720,0,1)`, colors `(1,1,1,1)`, and UVs `(0,0)`, `(1,0)`, `(1,1)`, `(0,1)`
- Draw `24` decoded vertex preview: three non-indexed vertices with format `57` positions `(-0.5,-0.5,0)`, `(639.5,-0.5,0)`, `(639.5,359.5,0)` and format `38` zero vectors

Current resource-backed D3D12 real replay:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\vertex_fetch_capture_001\events.jsonl --backend d3d12 --draw 1004 --d3d12-output native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp --no-summary
```

Result:

- Output: `native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp`
- SHA-256: `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Pixel check: `1280x720`, `921600/921600` non-clear pixels, RGB ranges `r=64..255`, `g=64..255`, `b=255..255`
- Drawn data: captured draw `1004` indices `3,0,2,2,0,1` and decoded position/color/UV vertices
- Shader status: diagnostic native shader only; BO2 shader translation/override is still missing

The Vulkan backend names are accepted but fail closed because no Vulkan backend is implemented in this tree yet.

Updated geometry replay result:

- Output: `C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-geometry-replay.bmp`
- Exit code: `0`
- Size: `3686454` bytes.
- Last write time: `2026-06-28 16:52:19`.
- SHA-256: `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`
- Visual check: nonblank, with D3D12 shader-pipeline draw tiles visible over the clear-tile layer.

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

First indexed draw after constants, draw index `1209`:

```text
draw[1209] seq=5334 frame=pre PM4_DRAW_INDX opcode=0x00000022 packet=0xC0032201 packet_ptr=0x0501B4F0 indices=6 prim=4 src=0 indexed=yes index_base=0x0501E0A0 index_len=12 index_fmt=0 endian=1
  VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 constants_total=2 constants_frame=2 last_constant_seq=5329 state= ok
```

Bound state for that draw currently has two ALU constant ranges, both missing payload in the old capture. Its index metadata is real packet data (`index_base=0x0501E0A0`, `index_len=12`, `index_format=0`, `endian=1`), but raw index bytes are not yet captured.

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
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-geometry-replay.out.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-geometry-replay.err.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-geometry-replay.bmp`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-replay-summary-updated.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-draw-bound-state-updated.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-updated.out.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-updated.err.log`
- `default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-updated.bmp`

The smoke capture validation result was:

```text
Validation OK: 100 events, 2 frames, 24 draws
```
