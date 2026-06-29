# Native Renderer D3D12 Status

Last updated: 2026-06-29

## Diagnostic backend

`--backend d3d12-diagnostic` works in offline replay.

Verified target-built command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12-diagnostic --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-target.bmp --d3d12-draws 4096 --no-summary
```

Result:

- Exit code: `0`
- Output: `native-renderer-d3d12-diagnostic-target.bmp`
- Size: `3686454`
- SHA-256: `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`

Latest index-payload capture command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\payload_capture_002\events.jsonl --backend d3d12-diagnostic --d3d12-output native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp --d3d12-draws 512 --no-summary
```

Result:

- Exit code: `0`
- Output: `native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp`
- Size: `3686454`
- SHA-256: `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`

Latest vertex-fetch capture command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\vertex_fetch_capture_001\events.jsonl --backend d3d12-diagnostic --d3d12-output native_captures\vertex_fetch_capture_001\native-renderer-d3d12-vertex-fetch-diagnostic.bmp --d3d12-draws 512 --no-summary
```

Result:

- Exit code: `0`
- Output: `native_captures\vertex_fetch_capture_001\native-renderer-d3d12-vertex-fetch-diagnostic.bmp`
- Size: `3686454`
- SHA-256: `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`

Latest shader-payload capture diagnostic command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\shader_payload_capture_001\events.jsonl --backend d3d12-diagnostic --d3d12-output native_captures\shader_payload_capture_001\native-renderer-d3d12-diagnostic-shader-payload.bmp --d3d12-draws 512 --no-summary
```

Result:

- Exit code: `0`
- Output: `native_captures\shader_payload_capture_001\native-renderer-d3d12-diagnostic-shader-payload.bmp`
- SHA-256: `6A3E73E74CC22538420B2FCDA6B5B7D72A4EA205EAAAD11476ED700554F6C577`

## Real backend

`--backend d3d12` now has a first offline resource-backed geometry path. It selects a captured indexed draw with real index and vertex snapshots, canonicalizes the decoded Xenos vertex data into a native D3D12 input layout, binds upload-buffer vertex/index resources, compiles a hash-keyed manual HLSL override pair when present, and submits `DrawIndexedInstanced`.

This is still not full BO2 scene rendering. Draws using the `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139` pair can now run without `--allow-diagnostic-shader` because a manual HLSL override pair exists under `shader_work\native_overrides`, and the pair can be reused through `shader_work\cache\shader_cache_index.json`. The override is interface-compatible with the currently canonicalized position/color/UV vertex stream and consumes a flattened captured constant payload block at `b1`; on `state_capture_004` it also samples captured format-6 texture SRVs through dynamic sampler descriptors and uses the first captured PSO-side render-state subset. It is not an automatic Xenos shader translation. Without a matching cache entry, override, translation, or explicit diagnostic fallback, `--backend d3d12` fails closed.

Current missing pieces:

- Automatic Xenos shader translation and persistent DXIL shader cache
- Complete captured constant-buffer layout binding beyond the current flattened `b1` root constants
- Complete texture tiling/format coverage and full sampler LOD/mip validation
- Render target/depth resources and full heterogeneous blend/raster/depth state
- Multi-draw/full-frame state sequencing for all captured draw types

Current captured pieces from `native_captures\vertex_fetch_capture_001`:

- Constant payloads: `84/84`
- Indexed draw snapshots: `31/31`, `372` raw index bytes total
- Vertex fetch records: `222`, all with bounded raw vertex payloads, `16892` raw vertex bytes total
- First indexed draw `1004`: decoded indices `3,0,2,2,0,1`, `vf95`, stride `32`, attributes `(format=38, offset=0)`, `(format=6, offset=16)`, `(format=37, offset=20)`, `128/128` vertex bytes, and decoded position/color/UV vertices for a `1280x720` quad
- Replay-side vertex decoding covers the observed formats `57`, `38`, `37`, and `6`, plus the common packed/integer/half/float variants implemented in `native_render_replay`
- Real D3D12 output for draw `1004`: `native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`, `1280x720`, `921600/921600` non-clear pixels
- Auto-selected real D3D12 output: `native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-auto-final.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Diagnostic D3D12 after the real path remains working: `native-renderer-d3d12-diagnostic-final.bmp`, SHA-256 `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`
- Fresh shader-payload capture `native_captures\shader_payload_capture_001`: `587/587` shader uploads with payload, `73/73` constant uploads with payload, `28` indexed draw snapshots, `218` vertex fetch snapshots
- Draw `1209` with explicit diagnostic shader fallback: `native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Draw `1209` with manual overrides `vs_5D918D91043B3ED0.hlsl` and `ps_C4ED2979F29C9139.hlsl`: `native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-constant-bound.bmp`, SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Draw `1209` with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, cache-only output SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Auto real replay without `--draw` now batches every fully supported draw using the first supported shader pair. For `shader_payload_capture_001`, it submits `11/11` captured draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139` and writes `native-renderer-d3d12-real-supported-pair-multidraw-log.bmp`, SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Draw `1209` with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root native_captures\empty_shader_cache` and no `--allow-diagnostic-shader`: exit code `1`, expected fail-closed message with the exact missing VS/PS hashes

Latest state-capture replay results:

- Capture: `native_captures\state_capture_004\events.jsonl`
- Validation: `Validation OK: 12000 events, 48 frames, 2475 draws`
- New replay data available to the backend: `344` texture fetch snapshots with `1409024` payload bytes and render state on all `2475` draws.
- Combined target draw: `799`, with real index bytes, `vf95` vertex bytes, four texture fetch records, two constant ranges, and render state.
- Diagnostic command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12-diagnostic --d3d12-output native-renderer-state-capture-004-d3d12-diagnostic.bmp --d3d12-draws 512 --no-summary`
- Diagnostic result: exit code `0`, output `native-renderer-state-capture-004-d3d12-diagnostic.bmp`
- Real command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-real.bmp --d3d12-draws 256 --no-summary`
- Real result: exit code `0`, `D3D12 real replay submitted 4 supported draw(s) for shader pair VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 out of 4 captured draw(s) with that pair`.
- Limitation for that pre-texture-binding run: the output remained the existing manual-override supported geometry path and was not full BO2 scene rendering.
- Texture-binding update: D3D12 real replay now creates a shader-visible SRV descriptor for the first decodable captured texture fetch per supported draw and binds it at `t0`. The current implementation supports captured Xenos texture format `6` (`k_8_8_8_8`) directly and decodes 2D tiled offsets with the Xenos address formula; unsupported formats or payloads that do not contain the required tiled footprint bind a white fallback texture and report the fallback count.
- Texture-bound command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-texture-bound.bmp --d3d12-draws 256 --no-summary`
- Texture-bound result: exit code `0`, `D3D12 real replay bound 4 captured texture SRV(s), 0 fallback texture SRV(s), unsupported_texture_attempts=0`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Cache-only texture-bound result with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, `4` captured texture SRVs bound, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Sampler-binding update: D3D12 real replay now uses a shader-visible sampler descriptor table at `s0` and creates one sampler descriptor per uploaded draw from captured texture-filter fields. Fresh captures also serialize Xenos clamp modes and map them to D3D12 address modes; old captures without those fields keep the previous clamp-addressing fallback.
- Sampler-bound command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-sampler-bound.bmp --d3d12-draws 256 --no-summary`
- Sampler-bound result: exit code `0`, `D3D12 real replay bound 4 captured sampler descriptor(s), 0 fallback sampler descriptor(s)`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Cache-only sampler-bound result with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, `4` captured texture SRVs and `4` captured sampler descriptors bound, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Clamp capture: `native_captures\clamp_capture_001\events.jsonl`
- Clamp capture validation: `Validation OK: 12000 events, 47 frames, 2448 draws`; resource summary reports `texture_fetch_records=344 ... clamp_modes=344/344 missing_clamp_modes=0`.
- Clamp-mapped D3D12 command: `native_render_replay.exe --capture native_captures\clamp_capture_001\events.jsonl --backend d3d12 --d3d12-output native-renderer-clamp-capture-001-d3d12-sampler-clamp.bmp --d3d12-draws 256 --no-summary`
- Clamp-mapped D3D12 result: exit code `0`, `D3D12 real replay bound 4 captured sampler descriptor(s), 0 fallback sampler descriptor(s), exact_clamp_modes=4, clamp_addressing_fallbacks=0`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Tiled format-6 replay command: `native_render_replay.exe --capture native_captures\clamp_capture_001\events.jsonl --backend d3d12 --d3d12-output native-renderer-clamp-capture-001-d3d12-tiled-format6.bmp --d3d12-draws 256 --no-summary`
- Tiled format-6 replay result: exit code `0`, `4` captured texture SRVs, `0` fallback texture SRVs, `exact_clamp_modes=4`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Old-capture clamp fallback result on `state_capture_004`: exit code `0`, `exact_clamp_modes=0, clamp_addressing_fallbacks=4`, output `native-renderer-state-capture-004-d3d12-sampler-clamp-fallback.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Render-state update: D3D12 real replay now builds the real replay PSO from the first supported draw's captured render state for rasterizer culling/front-face, fill mode, depth-clip, color write mask, blend factors/ops, and disabled depth/stencil state. It also applies captured screen scissor when a bounded scissor rectangle is present.
- Render-state command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-render-state.bmp --d3d12-draws 256 --no-summary`
- Render-state result: exit code `0`, `D3D12 real replay applied render state from draw 799: color_mask=0x0000000F cull=2 depth_test=no depth_write=no stencil=no`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Cache-only render-state result with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, same applied-state log and SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Sidecar texture-payload capture: `native_captures\sidecar_capture_002\events.jsonl`
- Sidecar validation: `Validation OK: 12000 events, 52 frames, 2628 draws`; resource summary reports `texture_fetch_records=412`, `texture_snapshots=412`, `sidecars=372`, `sidecar_bytes=3999744`, `payload_bytes=4004864`, `truncated=186`, and `sidecar_resource_manifest=present json=yes jsonl=yes`.
- Sidecar real command: `native_render_replay.exe --capture native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output native-renderer-sidecar-capture-002-d3d12-real.bmp --d3d12-draws 256 --no-summary`
- Sidecar real result: exit code `0`, `D3D12 real replay submitted 5 supported draw(s) for shader pair VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 out of 5 captured draw(s) with that pair`, `5` captured texture SRVs, `0` fallback texture SRVs, `5` captured sampler descriptors, `exact_clamp_modes=5`, applied render state from draw `748`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Sidecar diagnostic command: `native_render_replay.exe --capture native_captures\sidecar_capture_002\events.jsonl --backend d3d12-diagnostic --d3d12-output native-renderer-sidecar-capture-002-d3d12-diagnostic.bmp --no-summary`
- Sidecar diagnostic result: exit code `0`, output SHA-256 `538733ED1A0DEF3B4BA0FD0CA2F600C61742ACEDBE6E761BEACA62116F4E9327`.
- Frontbuffer sidecar capture: `native_captures\frontbuffer_capture_001\events.jsonl`
- Frontbuffer validation: `Validation OK: 3000 events, 16 frames, 673 draws`; resource summary reports `frontbuffer_snapshots=15`, `payload_bytes=55296000`, `sidecars=15`, `sidecar_bytes=55296000`, and `truncated=0`.
- Frontbuffer content status: the first observed `1280x720x4` sidecars are all zero, so the D3D12 backend cannot use PM4 swap payloads as scene color yet. The next D3D12 resource blocker is draw-time color/depth target snapshots from captured render-state target bases.
- Frontbuffer regression command on `sidecar_capture_002`: `native_render_replay.exe --capture native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output native-renderer-frontbuffer-regression-d3d12-real.bmp --d3d12-draws 256 --no-summary`
- Frontbuffer regression result: exit code `0`, `5` captured texture SRVs, `5` exact-clamp sampler descriptors, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Regression command on older `shader_payload_capture_001`: `native_render_replay.exe --capture native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --d3d12-output native-renderer-shader-payload-d3d12-texture-binding-regression.bmp --d3d12-draws 64 --no-summary`
- Regression result: exit code `0`, `D3D12 real replay bound 0 captured texture SRV(s), 11 fallback texture SRV(s), unsupported_texture_attempts=0`, output SHA-256 `49F99D11F992073E0DF9371E37EE57DC0522032336E44ADAAFC00CEDE12E3D2A`.
- Remaining limitation: these SRVs and sampler descriptors are now real captured resources/state in the command stream, PM4 swap/frontbuffer payload capture is implemented, and the first PSO-side render-state subset is applied. The backend still lacks additional texture formats/mips, draw-time render-target/depth resource snapshots, real DSV binding for depth-enabled draws, per-state PSO switching across heterogeneous draws, and full blend/depth/stencil coverage.

## Build note

The CMake build graph was regenerated with Visual Studio CMake. `default`, `native_render_replay`, and `native_shader_inspect` build successfully through Ninja under `VsDevCmd`; latest targeted override-path builds:

- `native_render_replay`: exit `0`, `native_render_replay.exe` size `1076736`, last write `2026-06-29 17:21:11`
- `native_shader_inspect`: exit `0`, `native_shader_inspect.exe` size `812544`, last write `2026-06-29 20:42:39`
