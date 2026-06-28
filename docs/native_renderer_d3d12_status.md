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

## Real backend

`--backend d3d12` is present as a fail-closed command path. It does not draw until decoded replay resources can be bound with real native shaders and render state.

Current missing pieces:

- Backend input layouts and D3D12 vertex/index buffer binding for decoded Xenos vertex data
- Replacement or translated native shaders
- Texture/sampler state
- Render target/depth/blend/raster state

Current captured pieces from `native_captures\vertex_fetch_capture_001`:

- Constant payloads: `84/84`
- Indexed draw snapshots: `31/31`, `372` raw index bytes total
- Vertex fetch records: `222`, all with bounded raw vertex payloads, `16892` raw vertex bytes total
- First indexed draw `1004`: decoded indices `3,0,2,2,0,1`, `vf95`, stride `32`, attributes `(format=38, offset=0)`, `(format=6, offset=16)`, `(format=37, offset=20)`, `128/128` vertex bytes, and decoded position/color/UV vertices for a `1280x720` quad
- Replay-side vertex decoding covers the observed formats `57`, `38`, `37`, and `6`, plus the common packed/integer/half/float variants implemented in `native_render_replay`
- Real D3D12 still fails closed with: `translated input layouts, native shader replacements/translations, texture/sampler state, and render-target/depth state`

## Build note

The CMake build graph was regenerated with Visual Studio CMake. `default`, `native_render_replay`, and `native_shader_inspect` build successfully through Ninja under `VsDevCmd`; latest verified artifact times are 2026-06-28 23:16-23:17 Brisbane time.
