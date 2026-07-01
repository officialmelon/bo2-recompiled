# Native Renderer Replay

Last updated: 2026-06-30

`native_render_replay.exe` is the first offline replay executable for the BO2 native renderer work. It reads the JSONL written by `native_renderer_capture_path`, reconstructs frame/draw/shader/constant state, and reports enough state per draw to drive backend bring-up without booting the game for every iteration.

The tool also has D3D12 replay backends. `d3d12-diagnostic` renders an offscreen BMP from replayed draw events using BO2-owned D3D12 commands, including a small HLSL shader pipeline and synthetic triangle draws. `d3d12` now renders the first supported captured indexed draw with real replayed vertex/index data and either a hash-keyed manual HLSL override pair or an explicitly requested diagnostic native shader. Neither path is a full BO2 scene renderer yet.

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
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\resolve_readback_capture_001\events.jsonl --dump-frontbuffer --frontbuffer-output native-renderer-resolve-readback-frontbuffer-preview-rgb.bmp --no-summary
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
- `--dump-frontbuffer`: export a PM4 swap frontbuffer payload as an RGBA8 BMP preview. By default it picks the first payload with nonzero RGB pixels; use `--frontbuffer-index <n>` to force a zero-based PM4 swap payload and `--frontbuffer-output <path>` to set the BMP path. Fresh captures with swap fetch0 metadata and a complete tiled payload decode through `fetch0_tiled_rgba8`; older/incomplete captures fall back to raw linear RGBA8 and print the exact missing tiled footprint.
- `--resource-summary`: report current replay resource snapshot coverage.
- `--shader-usage --top-shaders <n>`: show shader hash and shader-pair draw usage.
- `--backend null|offline|d3d12-diagnostic|d3d12|vulkan-diagnostic|vulkan`: backend selector. `d3d12-diagnostic` runs the offscreen D3D12 debug renderer. `d3d12` is the real resource-backed path and fails closed unless real translated/cached/override shaders are available or `--allow-diagnostic-shader` is explicitly supplied. `vulkan*` fails closed until a Vulkan backend exists.
- `--d3d12-output <path>`: BMP output path for the D3D12 replay backend.
- `--shader-override-root <path>`: root containing native shader overrides. Default: `shader_work\native_overrides`. The current D3D12 resolver looks under `<root>\d3d12` for filenames keyed by runtime shader hash, such as `vs_5D918D91043B3ED0.hlsl` and `ps_C4ED2979F29C9139.hlsl`.
- `--shader-cache-root <path>`: root containing compiled shader cache entries. Default: `shader_work\cache`. In strict D3D12 mode the current order is cache index hit, parsed override manifest, deterministic override filename, then fail-closed unless `--allow-diagnostic-shader` is present.
- `--d3d12-draws <count>`: number of replay draw tiles to render; default is `4096`.
- `--allow-diagnostic-shader`: permits `--backend d3d12` to use the temporary diagnostic shader fallback for resource-backed geometry bring-up. Output with this flag is not shader-correct BO2 rendering.
- `--validate`: parse/analyze only and return non-zero on parse errors or strict replay validation errors such as indexed draws with missing index snapshots.

## Verified readback-resolve frontbuffer capture

Input:

`C:\Users\braxt\bo2-recompiled\native_captures\resolve_readback_capture_001\events.jsonl`

Capture command:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\resolve_readback_capture_001\events.jsonl --native_renderer_capture_limit 3000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
```

Replay result:

- Validation: `Validation OK: 3000 events, 16 frames, 671 draws`.
- Resource summary: `frontbuffer_snapshots=15`, `frontbuffer_payload_bytes=55296000`, all sidecar-backed and untruncated.
- Sidecar scan: `13/15` frontbuffer payloads contain nonzero bytes; `9/15` contain nonzero RGB pixels. Color/depth target-base previews remain zero.
- `--dump-frontbuffer` selects snapshot `3` by default because it is the first payload with nonzero RGB pixels: `seq=508`, `frontbuffer=0x1DD38000`, `1280x720`, `nonzero_bytes=1822720`, `rgb_nonzero_pixels=911360`, `alpha_nonzero_pixels=911360`.
- Raw-linear BMP preview: `native-renderer-resolve-readback-frontbuffer-preview-rgb.bmp`, SHA-256 `8C5D3248BE49A258FFD111FFCA1E30D44FC758A1A8C557FC1F534E8EA5384B28`.
- Interpretation: enabling ReXGlue `readback_resolve=full` makes resolved frontbuffer memory visible to CPU capture. This older capture is still raw-linear-only because it lacks swap fetch0 metadata and only captured the visible `1280*720*4` byte count.

## Verified swap fetch0 frontbuffer decode

Input:

`C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl`

Capture command:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --native_renderer_capture_limit 3000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
```

Replay commands:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --resource-summary --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --dump-frontbuffer --frontbuffer-output C:\Users\braxt\bo2-recompiled\native-renderer-swap-fetch-003-frontbuffer-decoded.bmp --no-summary
```

Replay result:

- Validation: `Validation OK: 3000 events, 16 frames, 670 draws`.
- Resource summary: `frontbuffer_snapshots=15`, `payload_bytes=56524800`, `sidecars=15`, `sidecar_bytes=56524800`, `truncated=0`.
- ReXGlue now captures the swap texture fetch0 metadata and requests the Xenos tiled upper-bound footprint. For the default selected snapshot, visible bytes are `3686400`, but the tiled footprint is `3768320`.
- `--dump-frontbuffer` selected snapshot `3`, `seq=508`, `frontbuffer=0x1DD38000`, `1280x720`, `payload=3768320/3768320`, `decode_mode=fetch0_tiled_rgba8`.
- Fetch0 fields: `base=0x1DD38000`, `format=6`, `endian=0`, `tiled=yes`, `pitch=40`, `swizzle=0x00000A0A`.
- Decoded BMP: `native-renderer-swap-fetch-003-frontbuffer-decoded.bmp`, SHA-256 `CD6890DFB492C3D8124A3E41DBF1161B2AD3821A6CE365F8509775D2F42A414B`.
- Visual status: coherent untiled output, but the tested early frame is solid blue. This proves frontbuffer footprint/tiling/swizzle decode, not full native scene rendering.
- D3D12 regression on `sidecar_capture_002` is unchanged: `5` supported draws submitted, `5` captured texture SRVs, `5` captured samplers, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.

## Verified shader-payload capture

Input:

`C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl`

Replay result:

- Events: `12000`
- Parse errors: `0`
- Frames: `53`
- Draws: `2654`
- Shader uploads: `587`, all with payload, `13230` payload dwords
- Constant uploads: `73`, all with payload
- Indexed draw snapshots: `28`, all with payload
- Vertex fetch records: `218`, all with bounded raw vertex payloads
- Validation: `Validation OK: 12000 events, 53 frames, 2654 draws`

Target draw `1209`:

```text
draw[1209] seq=5335 frame=pre PM4_DRAW_INDX opcode=0x00000022 packet=0xC0032201 packet_ptr=0x0501B700 indices=6 prim=4 src=0 indexed=yes index_base=0x0501E0B0 index_len=12 index_fmt=0 endian=1 payload=12/12 bytes
  VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 constants_total=2 constants_frame=2 last_constant_seq=5330 vertex_fetches=1/1 state= ok
```

Decoded resources:

- Indices: `3,0,2,2,0,1`
- Vertex fetch: `vf95`, raw words `0x0501E033,0x10000082`, address `0x0501E030`, `128/128` bytes, stride `32`, endian `2`
- Attributes: `FMT_32_32_32_32_FLOAT` at offset `0`, `FMT_8_8_8_8` at offset `16`, `FMT_32_32_FLOAT` at offset `20`
- Decoded vertices: a `1280x720` quad with position/color/UV components

D3D12 behavior:

- `--backend d3d12 --draw 1209` loads manual overrides from `shader_work\native_overrides\overrides.json`, compiles/cache-writes D3D12 shader blobs if needed, binds flattened captured constant payloads at root slot `1` / HLSL `b1`, and writes `native-renderer-d3d12-real-draw1209-constant-bound.bmp`, SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`.
- `--backend d3d12` without `--draw` auto-selects the first supported real draw, then submits every later fully supported draw with the same shader pair until `--d3d12-draws` is reached. On `shader_payload_capture_001`, `--d3d12-draws 64` reports `11 supported draw(s)` out of `11` captured draws for the draw-1209 shader pair.
- `--backend d3d12 --draw 1209 --shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache` succeeds from `shader_work\cache\shader_cache_index.json` and compiled `.dxbc` blobs without override source, producing the same `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60` output.
- `--backend d3d12 --draw 1209 --shader-override-root native_captures\empty_shader_overrides --shader-cache-root native_captures\empty_shader_cache` fails closed without a cached or override shader pair and reports the missing `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139` pair.
- `--backend d3d12 --draw 1209 --allow-diagnostic-shader` writes `native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`.

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

The current frame markers come from `VdSwap`/present hooks, so some command-processor traffic can still be pre-frame from the replay tool's parser perspective. The D3D12 real replay path now has a sequence-inferred fallback for captures with frame markers but no frame-owned PM4 draws: frame `N` uses draw events with sequence numbers after frame `N - 1`'s boundary and through frame `N`'s boundary, instead of assigning every pre-frame draw to frame `0`. A real live backend still must capture CP execution and present boundaries from the same timeline rather than assuming all draw work appears between current `begin_frame` and `end_frame`.

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

## Shader Record Probe Replay

`native_render_replay.exe` now accepts:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_003\events.jsonl --shader-record-probes --max-draws 4 --no-summary
```

Verified result on `shader_probe_capture_003`:

- Validation: `Validation OK: 4514 events, 22 frames, 1026 draws`
- Probe count: `2`
- Primary snapshots: `2`
- Secondary snapshots: `2`
- The replay report decodes printable big-endian ASCII runs from primary and secondary dword snapshots.
- The replay report also normalizes strings containing `pimp_shader_` into a `shader_name=` line.

Captured shader-record names:

```text
pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb
pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb
```

Important caveat: this capture exits naturally before the older `shader_payload_capture_001` draw `1209` window. Use it for shader/material record-layout evidence, not as the primary full draw/resource capture. The current primary draw/resource capture remains `shader_payload_capture_001`.

## Texture And Render-State Replay

Fresh capture `native_captures\state_capture_004\events.jsonl` is the current primary capture for texture and render-state replay coverage:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --validate
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --resource-summary
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --draw 799 --dump-bound-state
```

Verified results:

- Validation: `Validation OK: 12000 events, 48 frames, 2475 draws`
- Shader payloads: `691/691`
- Constant payloads: `136/136`
- Index snapshots: `30`, `360` bytes
- Vertex fetch records: `251`, `40984` payload bytes, `50` truncated bounded vertex snapshots
- Texture fetch records: `344`, `344` bounded snapshots, `1409024` payload bytes, `0` missing, `0` truncated
- Render state: present on all `2475` draws
- Shader-record probes: `0` in this normal capture path

Draw `799` is the first combined target:

```text
draw[799] seq=3555 PM4_DRAW_INDX indices=6 prim=4 indexed=yes index_base=0x04FF24E0 index_len=12 index_fmt=0 endian=1
  VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 constants_total=2 vertex_fetches=1/1 texture_fetches=4/4 render_state=yes
```

The bound-state dump reports four pixel texture fetch bindings at `0x05B67000` / `0x05B6B000`, each decoded as 1x1 tiled format `6` with endian `2` and `4096` captured payload bytes, plus render state with `surface_pitch=1280`, `depth_base=608`, `depth_format=1`, color base `1328`, color mask `0x0000000F`, depth test/write disabled, stencil disabled, and cull mode `2`.

Current replay limitation: texture payloads and render-state registers are parsed and visible in replay, but the D3D12 real backend does not yet create SRVs/samplers from those texture snapshots or apply the captured render-target/depth/blend/raster state.

## D3D12 Shader-Pair Isolation

`native_render_replay.exe` supports filtering real D3D12 replay to one runtime
shader pair:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_004\events.jsonl --backend d3d12 --skip-unsupported --shader-pair 0x3C4F6D40D699817B:0xEDC17DCC3FFDB040 --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp004-pair-3c4f-edc1.bmp --no-summary
```

This is a diagnostic isolation flag only. Full offline replay and live
`native_d3d12` rendering remain unfiltered unless `--shader-pair` is supplied.
It is useful for separating BO2-derived output by shader class; for example,
MP004 shows `3C4F/EDC1` owns the honeycomb/background plus the corrupt green
upper-left texture smear, while `261B/FF01` owns the cleaner small
font/glyph-style marks.

Follow-up MP009 note: the green/pink `3C4F/EDC1` smear was traced to
block-compressed format `18/19/20` texture payloads that were still using the
16 KB ReXGlue/Xenos preview as if it were a complete resource. The SP and MP
hooks now recapture DXT/DXN payloads with a tiled address upper bound, and
`D3D12ReplayBackend` rejects truncated texture snapshots in real replay. On
`native_captures\live_d3d12_mp_009\events.jsonl`,
`--shader-pair 0x3C4F6D40D699817B:0xEDC17DCC3FFDB040` submits all `12/12`
draws with `unsupported_texture_attempts=0`, `partial_texture_previews=0`, and
`diagnostic_pipelines=0`; the isolated output
`native-renderer-mp009-pair-3c4f-edc1.bmp` shows the real BO2 title logo
instead of the prior smear.

Follow-up MP009 A4 note: the dark diagonal in the full D3D12 replay was
isolated to no-texture `PS=0xA4A965C189287B99` color-export passes:
`1E6883/A4` and `AB1E/A4`. These are now fail-closed in real replay because the
current limited translator cannot prove the Xenos `r0` source/export semantics
for those passes, and rendering them produced false fullscreen triangles. The
updated full replay output is
`native-renderer-mp009-a4-failclosed.bmp`; it submits `24` draws across the two
textured title/UI pairs with no diagnostic pipelines and no partial texture
previews. `--dump-bound-state` now also prints decoded blend/scissor/raster
state to make these fullscreen pass decisions auditable from the capture.
