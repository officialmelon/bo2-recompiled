# Native Renderer Real Backend Status

Last updated: 2026-06-29

## Status

Real native rendering is not complete.

The working GPU-output backends are:

- `d3d12-diagnostic`, which renders synthetic debug rectangles from captured BO2 events.
- `d3d12`, which now renders the first resource-backed captured BO2 indexed draw from replay data using real captured vertex and index buffers plus a diagnostic native shader.

Neither path is full BO2 scene rendering yet. The `d3d12` path proves native D3D12 vertex/index buffer binding and `DrawIndexedInstanced` with captured BO2 geometry, but it still lacks translated BO2 shaders, textures/samplers, render-target/depth state, and full-frame state replay.

## Real D3D12 gate

`native_render_replay.exe --backend d3d12` is reserved for the real resource-backed D3D12 backend. It now succeeds for captures with at least one supported indexed draw snapshot:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --backend d3d12 --draw 1004 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp --no-summary
```

- Output: `native-renderer-d3d12-real-draw1004-final.bmp`
- SHA-256: `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Pixel check: `1280x720`, `921600/921600` pixels differ from the clear color, RGB ranges `r=64..255`, `g=64..255`, `b=255..255`

The rendered geometry is the captured draw `1004` full-screen quad. The visible gradient comes from a diagnostic native shader using captured color/UV attributes, so this output is not shader-correct BO2 rendering.

## Current verified blocker

Draw `1004` in `native_captures\vertex_fetch_capture_001` has enough packet data, index bytes, constants, vertex fetch metadata, and bounded vertex bytes to identify a real indexed draw:

- Real: draw packet, index metadata, raw index bytes, decoded indices `3,0,2,2,0,1`, shader hashes, two constant ranges with payload, `vf95` at guest physical `0x05008230`, stride `32`, attributes with Xenos formats `38`, `6`, and `37`, `128/128` raw vertex bytes, and decoded position/color/UV components.
- Implemented: D3D12 canonical input layout, upload-buffer vertex/index resources, and `DrawIndexedInstanced` for this draw.
- Missing: shader replacements/translation, render target/depth state, texture/sampler state, and full-frame multi-draw replay.

## Next implementation targets

1. Add shader runtime-hash matching and manual overrides for the `5D918D91043B3ED0` / `C4ED2979F29C9139` pair.
2. Decode texture/sampler and render-target/depth/blend/raster state for the same frame.
3. Expand D3D12 real replay from one selected draw to all supported draws in the captured frame.
4. Add constant-buffer binding from captured payloads.
5. Add sidecar resource manifests to keep larger buffers/textures out of JSONL.
