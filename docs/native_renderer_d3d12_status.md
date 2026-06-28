# Native Renderer D3D12 Status

Last updated: 2026-06-28

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

## Real backend

`--backend d3d12` is present as a fail-closed command path. It does not draw until real captured resources and shaders exist.

Current missing pieces:

- Index-buffer byte snapshots
- Vertex/fetch decode and vertex-buffer byte snapshots
- Translated input layouts and topology mapping
- Replacement or translated native shaders
- Constant payloads in fresh captures
- Texture/sampler state
- Render target/depth/blend/raster state

## Build note

The CMake build graph was regenerated with Visual Studio CMake. `native_render_replay` and `native_shader_inspect` build successfully through Ninja under `VsDevCmd`. Full `default` was capped at 180 seconds and stopped while rebuilding ReXGlue runtime objects.
