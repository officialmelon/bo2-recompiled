# Native Renderer Validation

Last updated: 2026-06-28

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

## Current hard blocker

The current fresh capture can feed real index buffers for the first indexed draws, but it still cannot feed a real D3D12/Vulkan scene backend because it lacks vertex/fetch decode, raw vertex bytes, texture/sampler state, render-target/depth state, and replacement/translated shaders. Real backends intentionally fail closed rather than synthesize missing data.

## Next required work

1. Decode/capture vertex fetch constants and raw vertex buffers for draw `1209`.
2. Add sidecar resource manifests for larger vertex/texture/RT snapshots.
3. Bind captured indices into the real D3D12 replay path once vertex buffers are available.
4. Implement shader runtime-hash matching and manual override plumbing for the top replay shader pairs.
5. Decode texture/sampler and render-target/depth state for the same tested frame.
