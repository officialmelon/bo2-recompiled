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

This is still not full BO2 scene rendering. Draw `1209` can now run without `--allow-diagnostic-shader` only because a documented manual HLSL override pair exists under `shader_work\native_overrides\d3d12`. The override is interface-compatible with the currently canonicalized position/color/UV vertex stream, but it is not an automatic Xenos shader translation and it does not bind captured BO2 constant payloads, textures, or render state yet. Without a matching override, translation, or cache entry, `--backend d3d12` fails closed.

Current missing pieces:

- Automatic Xenos shader translation and persistent DXIL shader cache
- Captured constant-buffer binding into the native root signature
- Texture/sampler state
- Render target/depth/blend/raster state
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
- Draw `1209` with manual overrides `vs_5D918D91043B3ED0.hlsl` and `ps_C4ED2979F29C9139.hlsl`: `native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-manual-override.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Draw `1209` with `--shader-override-root native_captures\empty_shader_overrides` and no `--allow-diagnostic-shader`: exit code `1`, expected fail-closed message with the exact missing VS/PS hashes

## Build note

The CMake build graph was regenerated with Visual Studio CMake. `default`, `native_render_replay`, and `native_shader_inspect` build successfully through Ninja under `VsDevCmd`; latest targeted override-path builds:

- `native_render_replay`: exit `0`, `native_render_replay.exe` size `1076736`, last write `2026-06-29 17:21:11`
- `native_shader_inspect`: exit `0`, `native_shader_inspect.exe` size `812544`, last write `2026-06-29 20:42:39`
