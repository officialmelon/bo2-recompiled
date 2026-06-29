# Native Renderer Validation

Last updated: 2026-06-29

## Completion status

Native rendering is not complete. The current verified output is diagnostic D3D12 replay output, not BO2 scene rendering.

## 2026-06-28 index-payload pass

Full Windows build:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 default native_render_replay native_shader_inspect"
```

- Build log: `default\out\build\win-amd64-clangmsvc-debug\native-renderer-index-payload-build.out.log`
- Result: linked `default.exe`, `native_render_replay.exe`, and `native_shader_inspect.exe`.
- `default.exe`: `78921728` bytes, last write `2026-06-28 22:32:01`.
- `native_render_replay.exe`: `951808` bytes, last write `2026-06-28 22:33:38`.
- `native_shader_inspect.exe`: `693248` bytes, last write `2026-06-28 22:32:11`.
- `native-renderer-replay-rebuild.out.log` records the replay-only rebuild after adjusting non-validation exit behavior.

Fresh capture:

```powershell
default.exe --native_renderer_mode native --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\events.jsonl --native_renderer_capture_limit 25000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false
```

- Capture: `C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\events.jsonl`
- Events: `25000`
- Parse errors: `0`
- Draws: `5471`
- Constant payloads: `227/227`
- Indexed draw snapshots: `81`
- Missing indexed snapshots: `0`
- Index payload bytes: `972`

Validation:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\events.jsonl --validate
```

- Exit code: `0`
- Result: `Validation OK: 25000 events, 106 frames, 5471 draws`

First indexed draw with real index bytes:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\events.jsonl --draw 1209 --dump-bound-state --dump-constants --dump-indices --resource-summary --no-summary
```

- Draw: `PM4_DRAW_INDX`, `index_base=0x0501E090`, `index_len=12`, `index_format=0`, `endian=1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- Constants: two bound ranges, both with payload

D3D12 diagnostic replay:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\events.jsonl --backend d3d12-diagnostic --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp --d3d12-draws 512 --no-summary
```

- Exit code: `0`
- Output: `C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp`
- Size: `3686454` bytes
- SHA-256: `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`

Old capture validation behavior:

- `payload_capture_001` remains readable for dumps.
- `--validate` correctly fails with `indexed draws missing index snapshots: 83`.

## 2026-06-29 vertex-fetch pass

Full Windows build:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 default native_render_replay native_shader_inspect"
```

- Build log: `default\out\build\win-amd64-clangmsvc-debug\native-renderer-vertex-fetch-build.out.log`
- Result: linked `default.exe`, `native_render_replay.exe`, and `native_shader_inspect.exe`.
- `default.exe`: `78928896` bytes, last write `2026-06-28 23:16:27`.
- `native_render_replay.exe`: `998912` bytes, last write `2026-06-28 23:16:37`.
- `native_shader_inspect.exe`: `693248` bytes, last write `2026-06-28 23:16:37`.
- Replay-only rebuilds after validation-message fixes linked successfully via `native-renderer-vertex-fetch-replay-rebuild.out.log` and `native-renderer-d3d12-message-rebuild.out.log`.

Fresh capture:

```powershell
default.exe --native_renderer_mode native --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --native_renderer_capture_limit 12000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false
```

- Capture: `C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl`
- Events: `12000`
- Parse errors: `0`
- Draws: `2646`
- Constant payloads: `84/84`
- Indexed draw snapshots: `31`
- Vertex fetch records: `222`
- Vertex buffer snapshots: `222`
- Vertex payload bytes: `16892`

Validation:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --validate
```

- Exit code: `0`
- Result: `Validation OK: 12000 events, 53 frames, 2646 draws`

First indexed draw with real index and vertex bytes:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --draw 1004 --dump-bound-state --dump-indices --dump-vertices --no-summary
```

- Draw: `PM4_DRAW_INDX`, `index_base=0x050082B0`, `index_len=12`, `index_format=0`, `endian=1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- Vertex fetch: `vf95`, raw words `0x05008233 0x10000082`, address `0x05008230`, size `128`, stride `32`, endian `2`
- Attributes: Xenos formats `38`, `6`, and `37` at byte offsets `0`, `16`, and `20`
- Raw vertex payload: `128/128` bytes
- Decoded vertex preview: a four-vertex `1280x720` quad with position/color/UV components:
  - `(0,0,0,1)`, `(1,1,1,1)`, `(0,0)`
  - `(1280,0,0,1)`, `(1,1,1,1)`, `(1,0)`
  - `(1280,720,0,1)`, `(1,1,1,1)`, `(1,1)`
  - `(0,720,0,1)`, `(1,1,1,1)`, `(0,1)`
- Constants: two bound ranges, both with payload

Non-indexed draw vertex decode check:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --draw 24 --dump-vertices --no-summary
```

- Draw `24`: `PM4_DRAW_INDX_2`, `vf0`, address `0x04F955DC`, size `84`, stride `28`, endian `2`
- Attributes: Xenos formats `57` and `38`
- Decoded positions: `(-0.5,-0.5,0)`, `(639.5,-0.5,0)`, `(639.5,359.5,0)`

D3D12 diagnostic replay:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --backend d3d12-diagnostic --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\native-renderer-d3d12-vertex-fetch-diagnostic.bmp --d3d12-draws 512 --no-summary
```

- Exit code: `0`
- Output: `C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\native-renderer-d3d12-vertex-fetch-diagnostic.bmp`
- Size: `3686454` bytes
- SHA-256: `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`

Resource-backed D3D12 geometry replay:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --backend d3d12 --draw 1004 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp --no-summary
```

- Exit code: `0`
- Output: `C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp`
- SHA-256: `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Pixel check: `1280x720`, `921600/921600` non-clear pixels, RGB ranges `r=64..255`, `g=64..255`, `b=255..255`
- This is resource-backed captured BO2 geometry: D3D12 upload buffers are created from draw `1004` vertex/index snapshots and submitted with `DrawIndexedInstanced`.
- This is still not full BO2 native rendering: the shader is a diagnostic native HLSL shader, constants are not bound into the shader interface, and texture/sampler/render-target/depth state is not replayed.

## 2026-06-29 runtime shader matching pass

Build:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 native_shader_inspect"
```

- Build log: `default\out\build\win-amd64-clangmsvc-debug\native-renderer-shader-runtime-match-rebuild.out.log`
- Result: linked `native_shader_inspect.exe`

Runtime shader listing:

```powershell
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --list-runtime-shaders --top-shaders 20
```

- Lines: `12000`
- Shader events: `594`
- Draw events: `2646`
- Unique runtime shaders: `8`
- Runtime shader pairs: `7`
- Draw `1004` pair: VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`, `11` draws in the capture

Runtime shader direct static-index match:

```powershell
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --match-runtime-shaders --top-shaders 20
```

- Direct exact-substring matches: `0/8`
- Evidence: `No shader index record matched '5d918d91043b3ed0'`
- Blocker: runtime 64-bit shader hashes are not proven to equal static container or microcode hashes. The next capture work needs PM4 shader payload bytes and/or material shader record metadata.

## Commands run in this pass

Direct MSVC compile was used first to validate the changed replay tools without touching the generated build graph:

```powershell
cl /std:c++latest /EHsc /MDd NativeShaderInspect.cpp
cl /std:c++latest /EHsc /MDd NativeRenderReplay.cpp D3D12ReplayBackend.cpp NativeRenderReplayTool.cpp d3d12.lib d3dcompiler.lib
```

Results:

- Direct build exit: `0`
- Outputs: `native_shader_inspect_direct.exe`, `native_render_replay_direct.exe`

After regenerating the CMake build graph with Visual Studio CMake, the target-specific Ninja build succeeded:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""default\out\build\win-amd64-clangmsvc-debug"" -j1 native_render_replay native_shader_inspect"
```

- Exit code: `0`
- `native_render_replay.exe`: `934912` bytes
- `native_shader_inspect.exe`: `693248` bytes

The full `default` target was attempted with a 180-second cap. It was still rebuilding ReXGlue runtime objects at step `28/165`, so it was stopped intentionally. No compile error from the BO2 native renderer changes was reached in that bounded run.

Replay summary:

```powershell
native_render_replay_direct.exe --capture native-renderer-capture-limit.jsonl --summary --shader-usage --top-shaders 20
```

- Exit code: `0`
- Events: `20000`
- Draws: `4388`
- Constant payload coverage: `with_payload=0`, `missing_payload=170`

Replay smoke validation:

```powershell
native_render_replay.exe --capture native-renderer-final-smoke.jsonl --validate
```

- Exit code: `0`
- Result: `Validation OK: 100 events, 2 frames, 24 draws`

Bound state/resource check:

```powershell
native_render_replay.exe --capture native-renderer-capture-limit.jsonl --draw 1209 --dump-bound-state --dump-constants --dump-indices --dump-vertices --resource-summary --no-summary
```

- Exit code: `0`
- Confirms indexed draw metadata exists.
- Confirms raw index/vertex/resource snapshots are missing.

D3D12 diagnostic replay:

```powershell
native_render_replay.exe --capture native-renderer-capture-limit.jsonl --backend d3d12-diagnostic --d3d12-output native-renderer-d3d12-diagnostic-target.bmp --d3d12-draws 4096 --no-summary
```

- Exit code: `0`
- BMP size: `3686454`
- SHA-256: `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`

Real D3D12 replay:

```powershell
native_render_replay.exe --capture native-renderer-capture-limit.jsonl --backend d3d12 --no-summary
```

- Exit code: `1`
- Expected failure: missing real resource snapshots and replacement shaders.

Vulkan diagnostic replay:

```powershell
native_render_replay.exe --capture native-renderer-capture-limit.jsonl --backend vulkan-diagnostic --no-summary
```

- Exit code: `1`
- Expected failure: no Vulkan backend exists yet.

Shader index summary:

```powershell
native_shader_inspect.exe --index shader_work\shaders\index.json --summary
```

- Exit code: `0`
- Registry loaded successfully.

## Ghidra evidence

PC/PDB:

- `R_DrawIndexedPrimitive @ 0x00A8DB70` flushes dirty constant buffers immediately before the D3D indexed draw call.
- `RB_DrawTessSurface` derives `triCount = tess.indexCount / 3`, updates viewport/scissor, uploads tess indices, and dispatches the draw technique.

XEX:

- `PM4_DRAW_INDX_2` packet construction hits include `0x825829AC`, `0x82582C8C`, `0x8258A6C4`, and `0x8258CFA0`.
- `PM4_LOAD_ALU_CONSTANT` packet construction hit: `0x82597FC4`.
- `PM4_SET_SHADER_CONSTANTS` packet construction hit: `0x8258CE80`.
- `PM4_IM_LOAD_IMMEDIATE` shader upload hits include `0x82582948`, `0x8258CD20`, and related paths.

## 2026-06-29 shader-payload capture and D3D12 shader gate

Full Windows build:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 default native_render_replay native_shader_inspect"
```

- Build log: `default\out\build\win-amd64-clangmsvc-debug\native-renderer-shader-payload-build.out.log`
- Exit file: `native-renderer-shader-payload-build.exit.txt` = `0`
- Result: linked `default.exe`, `native_render_replay.exe`, and `native_shader_inspect.exe`

Fresh capture:

```powershell
default.exe --native_renderer_mode native --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --native_renderer_capture_limit 12000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false
```

- Capture process was explicitly stopped after the JSONL hit the bounded `12000` event limit.
- Capture: `C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl`
- Events: `12000`
- Frames: `53`
- Draws: `2654`
- Shader payloads: `587/587`, `13230` payload dwords, `0` missing, `0` truncated
- Constant payloads: `73/73`
- Indexed draw snapshots: `28`
- Vertex fetch snapshots: `218`
- Validation: `Validation OK: 12000 events, 53 frames, 2654 draws`

Target draw:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --draw 1209 --dump-bound-state --dump-indices --dump-vertices --dump-constants --no-summary
```

- Draw: `PM4_DRAW_INDX`, packet `0xC0032201`, packet pointer `0x0501B700`
- Index base: `0x0501E0B0`, length `12`, format `0`, endian `1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- VS: `0x5D918D91043B3ED0`, `63` payload dwords captured
- PS: `0xC4ED2979F29C9139`, `72` payload dwords captured
- Vertex fetch: `vf95`, address `0x0501E030`, stride `32`, `128/128` vertex bytes
- Attributes: Xenos formats `38`, `6`, `37` at byte offsets `0`, `16`, `20`
- Decoded vertex preview: a four-vertex `1280x720` quad with position/color/UV components
- Constants: two bound ranges, both with payload

Shader matching:

```powershell
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --list-runtime-shaders --match-runtime-shaders --top-shaders 12
```

- Unique runtime shaders: `8`
- Runtime shader pairs: `7`
- Direct runtime-hash static-index matches: `0/8`
- `native_shader_inspect.exe` now computes raw little-endian, raw big-endian, trailing-zero-trimmed little-endian, and trailing-zero-trimmed big-endian SHA-256 values for each captured runtime shader payload.
- All `8` runtime shaders report `payload_hash_mismatches=0`, so repeated uploads for the same runtime hash are stable in this capture.
- Static-index matches remain `0/8` for runtime IDs and for all four payload-hash variants.

Inspector rebuild used for payload-hash reporting:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 native_shader_inspect"
```

- Build log: `default\out\build\win-amd64-clangmsvc-debug\native-renderer-shader-inspect-payload-hashes-build.out.log`
- Exit file: `native-renderer-shader-inspect-payload-hashes-build.exit.txt` = `0`
- Result: linked `native_shader_inspect.exe`

Shader-record probe validation:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_003\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_003\events.jsonl --shader-record-probes --max-draws 4 --no-summary
```

- Build log: `default\out\build\win-amd64-clangmsvc-debug\native-renderer-shader-probe-rebuild.out.log`
- Exit file: `native-renderer-shader-probe-rebuild.exit.txt` = `0`
- Narrow replay rebuild logs: `native-renderer-replay-ascii-build.out.log` and `native-renderer-replay-shader-name-build.out.log`, both exit `0`
- Capture: `C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_003\events.jsonl`
- Validation: `Validation OK: 4514 events, 22 frames, 1026 draws`
- Shader-record probes: `2`, with `2` primary snapshots and `2` secondary snapshots
- Decoded names:
  - `pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb`
  - `pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb`
- Direct search of `534c8cc25dea1826410cc2974d7e9a80` and `e10bcefc8da60302d0bbf12b675d091c` in `shader_work\shaders\index.json` / `index.csv` returned no matches.
- `native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_003\events.jsonl --list-runtime-shaders --match-runtime-shaders --top-shaders 10` now reports probe stage guesses, normalized names, suffixes, and secondary pointer payload hashes.
- Probe-aware inspector result: `Runtime shader direct matches: 0/4`; `Shader record probe matches: names=0/2 suffixes=0/2 secondary_payloads=0/2`.
- Status: useful shader/material record identity evidence, but not a complete runtime-to-static shader mapping.

D3D12 gate:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --no-summary
```

- Exit code: `0` when the default override root contains the manual draw-1209 override pair.
- Output: `native-renderer-d3d12-real-draw1209-constant-bound.bmp`
- SHA-256: `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Status: real captured index/vertex resources plus flattened captured constants at `b1`, manual HLSL overrides parsed from `overrides.json`, and cached `.dxbc`; not automatic Xenos shader translation and not full scene rendering.

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_cache --no-summary
```

- Exit code: `1`
- Expected result: fails closed because no translated, cached, or override shader pair is available in the selected cache/override roots.

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --allow-diagnostic-shader --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp --no-summary
```

- Exit code: `0`
- Output: `native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp`
- SHA-256: `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Status: resource-backed captured geometry with an explicit diagnostic shader fallback, not shader-correct BO2 rendering.

State/texture/render-state capture validation:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\state_capture_004\events.jsonl --native_renderer_capture_limit 12000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\state_capture_004\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\state_capture_004\events.jsonl --resource-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\state_capture_004\events.jsonl --draw 799 --dump-bound-state
```

- Capture: `C:\Users\braxt\bo2-recompiled\native_captures\state_capture_004\events.jsonl`
- Validation: `Validation OK: 12000 events, 48 frames, 2475 draws`
- Shader payloads: `691/691`
- Constant payloads: `136/136`
- Index snapshots: `30`
- Vertex fetch records: `251`
- Texture fetch records: `344`, snapshots `344`, payload bytes `1409024`, missing `0`, truncated `0`
- Render state: `2475/2475` draws
- Shader-record probes: `0`
- Draw `799`: real indices, `vf95` vertex fetch bytes, four pixel texture fetch records, two constant ranges, and decoded render state (`surface_pitch=1280`, `depth_base=608`, `depth_format=1`, color base `1328`, depth test/write disabled, cull mode `2`)
- D3D12 diagnostic replay: exit code `0`, output `native-renderer-state-capture-004-d3d12-diagnostic.bmp`
- D3D12 real replay: exit code `0`, `4/4` submitted draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139`, output `native-renderer-state-capture-004-d3d12-real.bmp`
- D3D12 texture-bound replay: exit code `0`, `D3D12 real replay bound 4 captured texture SRV(s), 0 fallback texture SRV(s), unsupported_texture_attempts=0`, output `native-renderer-state-capture-004-d3d12-texture-bound.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`
- D3D12 texture-bound cache-only replay: exit code `0` with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`, output `native-renderer-state-capture-004-d3d12-texture-bound-cache-only.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`
- Older-capture regression: exit code `0`, `D3D12 real replay bound 0 captured texture SRV(s), 11 fallback texture SRV(s), unsupported_texture_attempts=0`, output `native-renderer-shader-payload-d3d12-texture-binding-regression.bmp`, SHA-256 `49F99D11F992073E0DF9371E37EE57DC0522032336E44ADAAFC00CEDE12E3D2A`
- Status: capture/replay now carries texture fetches and render state. D3D12 real replay binds captured format-6 texture SRVs for the supported shader pair, but does not yet map sampler state or apply render state.

## Current hard blocker

The current fresh captures can feed shader payloads, real index buffers, bounded raw vertex-buffer payloads, constant payloads, texture fetch payload prefixes, and decoded render-state registers for target draws. D3D12 can render the supported captured geometry with manifest-backed manual HLSL overrides, flattened captured constants, captured format-6 SRV binding, a compiled `.dxbc` cache hit, or the explicit diagnostic shader fallback. Runtime shader payload hashes are reproducible, and runtime shader/material record names are now captured, but neither currently maps directly to the extracted shader-work index. The renderer still cannot feed a real D3D12/Vulkan scene backend because it lacks automatic Xenos shader translation, DXC/DXIL, full texture tiling/format coverage, sampler-state mapping, render-target/depth resource snapshots, D3D12 render-state application, full constant-layout reconstruction, and full-frame sequencing.

Latest D3D12 multi-draw evidence: `native_render_replay.exe --backend d3d12 --d3d12-draws 64` on `shader_payload_capture_001` reports `11 supported draw(s)` out of `11` captured draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139`, output SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`.

## Next required work

1. Capture or recover material/shader record metadata for the `0x5D918D91043B3ED0` / `0xC4ED2979F29C9139` pair, then implement runtime-hash matching with evidence.
2. Replace flattened captured constants with a real shader-reflected constant-buffer layout.
3. Decode texture/sampler and render-target/depth/blend/raster state for the same tested frame.
4. Expand D3D12 strict replay from one selected draw to all supported draws in a captured frame.
5. Add DXC/DXIL and generated/translated shader cache entries.
6. Add sidecar resource manifests for larger vertex/texture/RT snapshots.
