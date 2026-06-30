# Native Renderer Validation

Last updated: 2026-06-29

## Completion status

Native rendering is not complete. The current verified output is diagnostic D3D12 replay output, not BO2 scene rendering.

## 2026-06-30 translated DXC cache pass

Targeted build:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j8 native_shader_inspect native_render_replay"
```

- Result: exit code `0`.
- Scope: project-local `native_shader_inspect.exe` and `native_render_replay.exe`; no `rexglue-sdk` edits.

Translated DXC compile validation:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-translated-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-translated-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-translated-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0x5D918D91043B3ED0 --compile-translated-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
```

- VS `0xB6C9863F710683EC`: generated `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.translated.dxc.hlsl`, compiled `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.translated.dxc.dxil`, `cache_hit=false` on first run and `cache_hit=true` on repeat.
- PS `0xA4A965C189287B99`: generated `shader_work\cache\hlsl\PS_0xA4A965C189287B99.translated.dxc.hlsl`, compiled `shader_work\cache\d3d12\PS_0xA4A965C189287B99.translated.dxc.dxil`, `cache_hit=false`.
- Unsupported real-resource VS `0x5D918D91043B3ED0`: exit code `1`, expected error `no limited translated-HLSL rule for VS 0x5D918D91043B3ED0`.

Status: this is a real non-diagnostic translated-shader cache path for a tiny verified Xenos subset. It is not a complete shader translator, and the limited translated DXIL pair has not yet been proven on a resource-backed rendered D3D12 draw.

JSONL cache resolver validation:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache-jsonl-replay-test --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-cache-jsonl-d3d12-real.bmp --no-summary
```

- Result: exit code `0`.
- Shader source: JSONL cache root only; override root was empty.
- Replay submitted `5/5` supported draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139`.
- Replay bound `5` captured texture SRVs, `5` captured samplers, and applied captured render state from draw `748`.
- Output SHA-256: `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.

Status: D3D12 real replay can now resolve non-diagnostic cache records from `shader_cache_index.jsonl` and accepts both `.dxbc` and `.dxil` blobs. The limited translated DXIL pair still needs a resource-backed capture/draw and compatible shader interface before it can be proven as rendered scene output.

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
- 2026-06-29 inspector update parses `pimp_shader_*` names into family, short hash, entry, and profile. On `shader_probe_capture_003`, the probes decode as `family=cinematic short_hash=519f564 entry=main profile=ps_3_0` and `family=radiant short_hash=190f4788 entry=main profile=vs_3_0`.
- Updated probe-aware index matching reports `Runtime shader direct matches: 0/4` and `Shader record probe matches: names=0/2 suffixes=0/2 short_hashes=0/2 secondary_payloads=0/2`, so the current `shader_work\shaders\index.json` still does not contain exact names, 32-hex suffixes, short hashes, or secondary-payload hashes for these probes.

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
- D3D12 render-state replay: exit code `0`, `D3D12 real replay applied render state from draw 799: color_mask=0x0000000F cull=2 depth_test=no depth_write=no stencil=no`, output `native-renderer-state-capture-004-d3d12-render-state.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`
- D3D12 render-state cache-only replay: exit code `0` with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`, output `native-renderer-state-capture-004-d3d12-render-state-cache-only.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`
- Clamp capture: `native_captures\clamp_capture_001\events.jsonl`, validation `Validation OK: 12000 events, 47 frames, 2448 draws`, resource summary `texture_fetch_records=344 ... clamp_modes=344/344 missing_clamp_modes=0`
- D3D12 exact-clamp replay: exit code `0`, `D3D12 real replay bound 4 captured sampler descriptor(s), 0 fallback sampler descriptor(s), exact_clamp_modes=4, clamp_addressing_fallbacks=0`, output `native-renderer-clamp-capture-001-d3d12-sampler-clamp.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`
- D3D12 tiled format-6 replay: exit code `0`, `D3D12 real replay bound 4 captured texture SRV(s), 0 fallback texture SRV(s), unsupported_texture_attempts=0`, output `native-renderer-clamp-capture-001-d3d12-tiled-format6.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`
- D3D12 old-capture clamp fallback: exit code `0` on `state_capture_004`, `exact_clamp_modes=0, clamp_addressing_fallbacks=4`, output `native-renderer-state-capture-004-d3d12-sampler-clamp-fallback.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`
- Older-capture regression: exit code `0`, `D3D12 real replay bound 0 captured texture SRV(s), 11 fallback texture SRV(s), unsupported_texture_attempts=0`, output `native-renderer-shader-payload-d3d12-texture-binding-regression.bmp`, SHA-256 `49F99D11F992073E0DF9371E37EE57DC0522032336E44ADAAFC00CEDE12E3D2A`
- Sidecar capture command used the rebuilt `default.exe` with `--native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_limit 12000 --native_renderer_capture_flush_interval 128`.
- Sidecar capture: `native_captures\sidecar_capture_002\events.jsonl`, validation `Validation OK: 12000 events, 52 frames, 2628 draws`.
- Sidecar resource summary: `texture_fetch_records=412`, `texture_snapshots=412`, `payload_bytes=4004864`, `sidecars=372`, `sidecar_bytes=3999744`, `truncated=186`, `sidecar_resource_manifest=present json=yes jsonl=yes`.
- Sidecar manifests: `resources\index.json` parses with `372` resources, and `resources\index.jsonl` contains `372` lines.
- Sidecar D3D12 real replay: exit code `0`, `D3D12 real replay submitted 5 supported draw(s) for shader pair VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 out of 5 captured draw(s) with that pair`, `5` captured texture SRVs, `0` fallback texture SRVs, `5` captured sampler descriptors, `exact_clamp_modes=5`, output `native-renderer-sidecar-capture-002-d3d12-real.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Sidecar D3D12 diagnostic replay: exit code `0`, output `native-renderer-sidecar-capture-002-d3d12-diagnostic.bmp`, SHA-256 `538733ED1A0DEF3B4BA0FD0CA2F600C61742ACEDBE6E761BEACA62116F4E9327`.
- Status: capture/replay now carries texture fetches, texture clamp modes, sidecar texture payload resources, and render state. D3D12 real replay binds captured format-6 texture SRVs from inline or sidecar payloads, binds per-draw sampler descriptors from captured texture-filter/clamp fields for the supported shader pair, and applies the first captured PSO-side render-state subset, but does not yet bind real depth targets or handle heterogeneous per-draw PSO changes.

Frontbuffer sidecar validation:

```powershell
cmd.exe /v:on /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 default native_render_replay"
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\frontbuffer_capture_001\events.jsonl --native_renderer_capture_limit 3000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\frontbuffer_capture_001\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\frontbuffer_capture_001\events.jsonl --resource-summary --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-frontbuffer-regression-d3d12-real.bmp --d3d12-draws 256 --no-summary
```

- Build exit code: `0` with `-j12`, elapsed about `145` seconds.
- Fresh capture process was watchdog-stopped after the bounded capture completed; `events.jsonl` is complete and the resource manifests are finalized.
- Validation: `Validation OK: 3000 events, 16 frames, 673 draws`.
- Resource summary: `frontbuffer_snapshots=15`, `missing=0`, `payload_bytes=55296000`, `sidecars=15`, `sidecar_bytes=55296000`, `truncated=0`.
- Manifest: `resources\index.json` parses with `15` resources; all are `frontbuffer_payload` entries of `3686400` bytes.
- Payload content check: first three sidecars are all-zero and share SHA-256 `0C660F2BD3EFF3150DD0040789ABE2291613B9AF319DF870203D4F77A4913A5F`.
- D3D12 regression on `sidecar_capture_002`: exit code `0`, still submits `5/5` supported draws, binds `5` captured texture SRVs, binds `5` exact-clamp sampler descriptors, and writes `native-renderer-frontbuffer-regression-d3d12-real.bmp` with SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Status: PM4 swap/frontbuffer memory capture is implemented and replay-loadable. It does not yet provide useful scene color for the tested early frames; draw-time render-target/depth snapshots are still required.

Draw-time render-target preview validation:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\target_snapshot_capture_003\events.jsonl --native_renderer_capture_limit 1800 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\target_snapshot_capture_003\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\target_snapshot_capture_003\events.jsonl --resource-summary --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\target_snapshot_capture_003\events.jsonl --draw 29 --dump-bound-state --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-target-snapshot-regression-d3d12-real.bmp --d3d12-draws 256 --no-summary
```

- Build exit code before capture: `0` for `default native_render_replay` with `-j12`.
- Validation: `Validation OK: 1800 events, 10 frames, 395 draws`.
- Resource summary: `color_target_snapshots=368`, `color_target_payload_bytes=6029312`, `depth_target_snapshots=369`, `depth_target_payload_bytes=6045696`, all sidecar-backed and truncated to 16 KiB previews.
- Manifest: `746` resources total, with `368` `color_target_payload`, `369` `depth_target_payload`, and `9` `frontbuffer_payload` entries.
- Draw `29`: color target base `1328`, depth base `608`, `payload=16384/3686400`, `offset=1843200`, sidecar-backed and truncated for both color and depth.
- Payload content check: scanning every color/depth target sidecar in `target_snapshot_capture_003` found only zero bytes.
- Expected vs actual: expected bounded target previews to contain at least some nonzero scene/depth data after sampling away from the top-left; actual CPU memory copies from target base ranges are all zero. This points at missing GPU-side ReXGlue/Xenos render-target backing/readback or resolve instrumentation rather than a replay parser issue.
- D3D12 regression on `sidecar_capture_002`: exit code `0`, unchanged SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.

Readback-resolved frontbuffer validation:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\resolve_readback_capture_001\events.jsonl --native_renderer_capture_limit 3000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\resolve_readback_capture_001\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\resolve_readback_capture_001\events.jsonl --resource-summary --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\resolve_readback_capture_001\events.jsonl --dump-frontbuffer --frontbuffer-output C:\Users\braxt\bo2-recompiled\native-renderer-resolve-readback-frontbuffer-preview-rgb.bmp --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-frontbuffer-dump-regression-d3d12-real.bmp --d3d12-draws 256 --no-summary
```

- Build exit code for `native_render_replay`: `0` with `-j12`.
- Capture run used a `180` second watchdog and was stopped after writing a valid capture.
- Validation: `Validation OK: 3000 events, 16 frames, 671 draws`.
- Resource summary: `frontbuffer_snapshots=15`, `frontbuffer_payload_bytes=55296000`, `sidecars=15`, `truncated=0`; color/depth target previews remain zero.
- Frontbuffer sidecar scan: `13/15` contain nonzero bytes, and `9/15` contain nonzero RGB pixels.
- `--dump-frontbuffer`: selected snapshot `3`, `seq=508`, `frontbuffer=0x1DD38000`, `size=1280x720`, `payload=3686400/3686400`, `nonzero_bytes=1822720`, `rgb_nonzero_pixels=911360`, `alpha_nonzero_pixels=911360`.
- Raw-linear preview output: `native-renderer-resolve-readback-frontbuffer-preview-rgb.bmp`, SHA-256 `8C5D3248BE49A258FFD111FFCA1E30D44FC758A1A8C557FC1F534E8EA5384B28`.
- Expected vs actual: expected `readback_resolve=full` to make ReXGlue-resolved frontbuffer bytes CPU-visible; actual capture does contain nonzero frontbuffer payloads. This older preview is not final scene-correct output because that capture lacks fetch0 format/swizzle/tiling/endian metadata used by ReXGlue's swap texture path.
- D3D12 regression on `sidecar_capture_002`: exit code `0`, unchanged SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.

Swap fetch0 decoded frontbuffer validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 default native_render_replay"
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --native_renderer_capture_limit 3000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --resource-summary --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_003\events.jsonl --dump-frontbuffer --frontbuffer-output C:\Users\braxt\bo2-recompiled\native-renderer-swap-fetch-003-frontbuffer-decoded.bmp --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-swap-fetch-footprint-regression-d3d12-real.bmp --d3d12-draws 256 --no-summary
```

- Build exit code: `0` with `-j12`, elapsed about `155` seconds.
- Capture run used a `210` second watchdog and was stopped after writing a valid capture.
- Validation: `Validation OK: 3000 events, 16 frames, 670 draws`.
- Resource summary: `frontbuffer_snapshots=15`, `frontbuffer_payload_bytes=56524800`, `sidecars=15`, `sidecar_bytes=56524800`, `truncated=0`.
- Frontbuffer decode: selected snapshot `3`, `seq=508`, `frontbuffer=0x1DD38000`, `size=1280x720`, `payload=3768320/3768320`, `decode_mode=fetch0_tiled_rgba8`.
- Fetch0 metadata: `format=6`, `endian=0`, `tiled=yes`, `pitch=40`, `swizzle=0x00000A0A`.
- Output: `native-renderer-swap-fetch-003-frontbuffer-decoded.bmp`, SHA-256 `CD6890DFB492C3D8124A3E41DBF1161B2AD3821A6CE365F8509775D2F42A414B`.
- Expected vs actual: expected the prior `3686400` byte sidecar to be too short for a tiled `1280x720` format-6 texture; actual replay now reports the old missing footprint as `3768320` bytes and the fresh capture decodes successfully. The decoded early frame is solid blue, so this is frontbuffer texture decode evidence, not final scene rendering.
- D3D12 regression on `sidecar_capture_002`: exit code `0`, unchanged SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.

Safe shader-record probe validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 default"
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode on --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --native_renderer_capture_limit 5000 --native_renderer_capture_flush_interval 32 --native_renderer_verbose false
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --shader-record-probes --max-draws 16 --no-summary
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --list-runtime-shaders --match-runtime-shaders --top-shaders 24
```

- Code change: project-local shader/material probe reads are guarded against Windows access violations, and `native_renderer_shader_record_probe_dwords` can cap snapshots for triage. Default is `32` dwords because `16` dwords truncates names before the profile/suffix fields.
- Build status: `default.exe` relinked successfully. The build graph repeatedly rebuilt generated/default objects and printed `ninja: warning: premature end of file; recovering`; no compile/link errors were emitted, and `default.exe` timestamp updated to `2026-06-30 10:21:54`.
- Capture run used a `175` second watchdog and was stopped after the bounded event limit.
- Validation: `Validation OK: 5000 events, 24 frames, 1106 draws`.
- Shader inspection: `233` shader events, `233/233` shader payloads, `8` unique runtime shaders, `7` shader pairs, and `24` shader-record probes.
- Full probe names recovered:
  - PS `pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb`
  - VS `pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb`
  - PS `pimp_shader_trivial_63c48fc7_ps_main_ps_3_0_cd38a0f731a8c984a57c101f28952e56.updb`
  - VS `pimp_shader_trivial_63c48fc7_vs_main_vs_3_0_6c5717313f11297d9b331e326b80df12.updb`
- Static-index matching remains unproven: `Runtime shader direct matches: 0/8`; `Shader record probe matches: names=0/24 suffixes=0/24 short_hashes=0/24 secondary_payloads=0/24`.
- Shader-record/runtime PM4 prefix matching: `native_shader_inspect.exe` now reports `Shader record probe runtime payload-prefix matches: 12/24` on `shader_probe_capture_007`.
  - `pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb` -> runtime PS `0xC4ED2979F29C9139`, `16` matched payload dwords at secondary offset `16`.
  - `pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb` -> runtime VS `0x5D918D91043B3ED0`, `16` matched payload dwords at secondary offset `16`.
  - `pimp_shader_trivial_63c48fc7_vs_main_vs_3_0_6c5717313f11297d9b331e326b80df12.updb` -> runtime VS `0x81311AC4B1FBD082`, `16` matched payload dwords at secondary offset `16`.
- The trivial PS record remains unmatched by this prefix rule in the first `32` secondary dwords; likely next evidence is a larger safe secondary snapshot or the `0x82597DF8` bind-call arguments.
- Ghidra MCP evidence: `LR=0x82598314` passes `r4=r29+0x28`, `r5=*(r29+0x18)` into `0x82597F50`; `LR=0x82598560` passes `r4=r30+0x368`, `r5=*(r30+0x20)` into `0x82597F50`. The surrounding XEX state binder later calls `0x82597DF8`, which is the next shader-name-to-PM4-hash correlation target.

Shader container and microcode inspection validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_shader_inspect"
native_shader_inspect.exe --shader C:\Users\braxt\bo2-recompiled\shader_work\shaders\containers\pixel_00011f10fef6a0b01806b897b1e93287c2622b233a5ed9f39d8a983d45226ae9.bin --dump-header
native_shader_inspect.exe --shader C:\Users\braxt\bo2-recompiled\shader_work\shaders\containers\vertex_002ee957c754be9d2a671f0dc0b63fca861b2398f5483703ec93f861466bf80d.bin --dump-header
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.ucode --dump-words --limit 16
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\vertex_4cbe8078e63245600ee471c244caa9b01c3943e54859b5ce9cabeb96d961553b.ucode --disassemble --limit 12
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.ucode --write-disasm C:\Users\braxt\bo2-recompiled\shader_work\out\disasm
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.ucode --write-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\vertex_4cbe8078e63245600ee471c244caa9b01c3943e54859b5ce9cabeb96d961553b.ucode --write-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --match-runtime-shaders --top-shaders 24
```

- Build status: focused `native_shader_inspect` target linked successfully with `-j12` in about `23` seconds; stderr only contained Ninja's build-log recovery warning.
- Pixel container header: flags `0x102A1100`, stage guess `pixel`, file bytes `3452`, virtual size `1508`, physical size `1944`, name offset `0x00000090`, metadata offset `0x00000574`, microcode descriptor offset `0x000005A4`, descriptor size candidate `1752`, shader name `pimp_shader_sw4_3d_char_skin_tension_5eee5b1e_ps_main_ps_3_0_ee259e07f134def2b4abdec00183c00f.updb`.
- Vertex container header: flags `0x102A1101`, stage guess `vertex`, file bytes `2716`, virtual size `1224`, physical size `1492`, name offset `0x00000080`, metadata offset `0x00000444`, microcode descriptor offset `0x0000046C`, descriptor size candidate `1428`, shader name `pimp_shader_treecanopy_2e501d62_vs_main_vs_3_0_d9a8545395d8ec777b8591bb6e2feac1.updb`.
- Pixel microcode inspection: file bytes `1752`, `438` dwords, recovered `pimp_technique_sw4_3d_char_skin_tension_9644a939` and `pimp_shader_sw4_3d_char_skin_tension_42bbfcd4.hlsl`, then dumped big-endian dwords.
- Vertex microcode inspection: file bytes `1428`, `357` dwords, recovered `pimp_technique_treecanopy_fd5f4522` and `pimp_shader_treecanopy_b03ff570.hlsl`, then emitted an unknown-preserving raw Xenos dword listing.
- Raw disassembly artifact writing: `--write-disasm C:\Users\braxt\bo2-recompiled\shader_work\out\disasm` creates `pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.xenos.asm` and prints the written path. The file is a full raw listing, not a decoded shader IR.
- Raw IR artifact writing: `--write-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir` creates `pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.bo2shaderir.json`, schema `bo2shaderir.raw_xenos.v1`, with `438` unresolved instruction nodes and empty semantic resource arrays. The same path was checked on vertex microcode `vertex_4cbe8078e63245600ee471c244caa9b01c3943e54859b5ce9cabeb96d961553b.ucode`, producing `357` unresolved nodes.
- Regression shader matching on `shader_probe_capture_007` is unchanged: direct runtime/static matches remain `0/8`, direct shader-record matches remain `0/24`, and conservative runtime payload-prefix matches remain `12/24`.
- Status: this is shader container parsing, raw microcode listing, and raw unresolved IR artifact infrastructure. It is not yet a true Xenos shader disassembler, semantic backend-neutral shader IR, HLSL/DXIL generation, or SPIR-V generation.

Runtime semantic shader disassembly validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j8 native_shader_inspect native_render_replay"
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --semantic-disassemble
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --semantic-disassemble
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-semantic C:\Users\braxt\bo2-recompiled\shader_work\out\semantic --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-semantic C:\Users\braxt\bo2-recompiled\shader_work\out\semantic --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-translated-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-translated-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0x5D918D91043B3ED0 --write-translated-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0x5D918D91043B3ED0 --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xC4ED2979F29C9139 --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --shader-usage
```

- Build status: focused `native_shader_inspect` and `native_render_replay` targets build successfully with the semantic analyzer wired in. The inspector links minimal ReXGlue shader analyzer sources directly and does not link the full `rex::graphics` target.
- Runtime VS `0xB6C9863F710683EC`: semantic analysis succeeds with `payload_dwords=24`, `cf_pair_index_bound=3`, `register_static_address_bound=2`, and decoded disassembly containing `exec`, `alloc interpolators`, `alloc position`, `max o0.0000, r0, r0`, and `max oPos.0001, r1, r1`.
- Runtime PS `0xA4A965C189287B99`: semantic analysis succeeds with `payload_dwords=9`, `cf_pair_index_bound=1`, `register_static_address_bound=1`, and decoded disassembly containing `alloc interpolators`, `exece`, and `max o0, r0, r0`.
- Runtime semantic artifact writing succeeds for the same top pair:
  - `shader_work\out\semantic\VS_0xB6C9863F710683EC.xenos.semantic.txt`
  - `shader_work\cache\ir\VS_0xB6C9863F710683EC.semantic.bo2shaderir.json`
  - `shader_work\out\semantic\PS_0xA4A965C189287B99.xenos.semantic.txt`
  - `shader_work\cache\ir\PS_0xA4A965C189287B99.semantic.bo2shaderir.json`
- Diagnostic HLSL scaffold writing succeeds:
  - `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.diagnostic.hlsl`
  - `shader_work\cache\hlsl\PS_0xA4A965C189287B99.diagnostic.hlsl`
  These files are explicitly diagnostic/interface scaffolding and are not counted as real Xenos translation.
- Diagnostic D3D12 shader compilation succeeds through `D3DCompile`:
  - `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.diagnostic.dxbc`
  - `shader_work\cache\d3d12\PS_0xA4A965C189287B99.diagnostic.dxbc`
  - `shader_work\cache\logs\VS_0xB6C9863F710683EC.diagnostic.log`
  - `shader_work\cache\logs\PS_0xA4A965C189287B99.diagnostic.log`
  - `shader_work\cache\diagnostic_shader_cache_index.jsonl`
  This is a generated diagnostic shader-cache proof only; it is not DXC/DXIL and not real Xenos shader translation.
- Diagnostic DXC/DXIL shader compilation succeeds:
  - DXC auto-discovered at `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe`.
  - `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.diagnostic.dxc.dxil`
  - `shader_work\cache\d3d12\PS_0xA4A965C189287B99.diagnostic.dxc.dxil`
  - `shader_work\cache\logs\VS_0xB6C9863F710683EC.diagnostic.dxc.dxc.log`
  - `shader_work\cache\logs\PS_0xA4A965C189287B99.diagnostic.dxc.dxc.log`
  - `shader_work\cache\shader_cache_index.jsonl`
  - Repeated VS compile reports `cache_hit=true`.
  This is still diagnostic shader-cache plumbing and is not counted as real Xenos shader translation.
- Limited translated-HLSL subset succeeds for the top runtime shader pair:
  - `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.translated.hlsl`
  - `shader_work\cache\hlsl\PS_0xA4A965C189287B99.translated.hlsl`
  - `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.translated.dxil`
  - `shader_work\cache\d3d12\PS_0xA4A965C189287B99.translated.dxil`
  - The subset lowers only `max o0.0000, r0, r0`, `max oPos.0001, r1, r1`, and `max oC0, r0, r0`.
  - Unsupported shader `0x5D918D91043B3ED0` fails closed with `no limited translated-HLSL rule`.
- Runtime semantic IR interface metadata now serializes non-empty fetch state for the current supported real-resource shader pair:
  - VS `0x5D918D91043B3ED0`: fetch constant `95`, stride `8` dwords, Xenos attribute formats `38`, `6`, and `37`.
  - PS `0xC4ED2979F29C9139`: four `tfetch2D` bindings on texture fetch constants `4`, `3`, `2`, and `1`.
- Replay shader usage on `shader_probe_capture_007` still parses `5000` events, `1106` draws, `8` unique runtime shaders, `7` shader pairs, and `233/233` shader payload uploads with payload.
- Static extracted `.ucode` semantic decode is not accepted as solved. The raw static commands still work, but static `.ucode` files include extracted metadata/constants before the analyzer's expected payload; an attempted offset scan was removed because wrong offsets can run too long. The next reverse-engineering target is the extracted `.ucode` file layout/container descriptor boundary, or direct semantic IR generation from captured runtime PM4 payloads.

Ghidra-backed shader binder evidence from this pass:

- XEX `0x82597DF8` reads the pass-relative shader record at `r5 + ((r10 + 0x70) << 3)`, reads microcode byte size from `record + 0x36c`, reads microcode offset from `record + 0x368`, adds pass base `*(r5 + 0x20)`, emits the shader-load PM4 packet, and copies the payload bytes.
- XEX `0x82598140` calls `0x82597F50` at `0x82598310` for PS-side shader argument data and has the only current xrefs to `0x82597DF8` at `0x825985d8` and `0x825986a8`.
- PC/PDB `Material_LoadPassVertexShader` and `Material_LoadPassPixelShader` register shader names and call `Material_SetPassShaderArguments_DX`; the PC function uses D3D reflection to produce argument/semantic metadata. The XEX path still needs equivalent metadata recovery from material records or Xenos shader decode.

## Current hard blocker

The current fresh captures can feed shader payloads, real index buffers, bounded raw vertex-buffer payloads, constant payloads, sidecar-backed texture payloads, texture clamp modes, PM4 swap/frontbuffer sidecar payloads with fetch0 metadata, bounded draw-time color/depth target previews, and decoded render-state registers for target draws. With `readback_resolve=full`, PM4 swap/frontbuffer sidecars can contain nonzero resolved output bytes, and replay can decode the tested format-6 tiled frontbuffer through captured fetch0 metadata. D3D12 can render the supported captured geometry with manifest-backed manual HLSL overrides, flattened captured constants, captured 2D format-6 SRV binding from inline or sidecar payloads, captured texture-filter/clamp sampler descriptors, first-pass captured render-state PSO setup, a compiled `.dxbc` cache hit, or the explicit diagnostic shader fallback. Runtime shader payload hashes are reproducible, runtime shader/material record names are captured, and three record-name families now map to runtime PM4 hashes through exact stage-matched payload-prefix evidence. The mappings still do not identify static `shader_work` containers/microcode, and the trivial PS record is unresolved. The renderer still cannot feed a real D3D12/Vulkan scene backend because it lacks automatic Xenos shader translation, DXC/DXIL, broader texture format/mip coverage, GPU-side render-target/depth readback or resolve snapshots for useful scene targets, real DSV binding for depth-enabled draws, full constant-layout reconstruction, heterogeneous PSO/state sequencing, full-frame sequencing, live native D3D12 output, and any Vulkan backend.

Latest D3D12 multi-draw evidence: `native_render_replay.exe --backend d3d12 --d3d12-draws 64` on `shader_payload_capture_001` reports `11 supported draw(s)` out of `11` captured draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139`, output SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`.

## Next required work

1. Capture or recover material/shader record metadata for the `0x5D918D91043B3ED0` / `0xC4ED2979F29C9139` pair, then implement runtime-hash matching with evidence.
2. Replace flattened captured constants with a real shader-reflected constant-buffer layout.
3. Expand D3D12 strict replay from one selected draw to all supported draws in a captured frame.
4. Add DXC/DXIL and generated/translated shader cache entries.
5. Add sidecar payload capture for larger vertex and render-target/depth snapshots.
6. Use the decoded frontbuffer path as a reference for swap/resolve validation while moving native D3D12 from selected-draw replay to full-frame replay.
