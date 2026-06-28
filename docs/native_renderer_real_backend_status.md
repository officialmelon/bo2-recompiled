# Native Renderer Real Backend Status

Last updated: 2026-06-28

## Status

Real native rendering is not complete.

The only working GPU-output backend is `d3d12-diagnostic`, which renders synthetic debug rectangles from captured BO2 events. It is useful for device, command-list, readback, and replay-state validation, but it is not BO2 scene rendering.

## Real D3D12 gate

`native_render_replay.exe --backend d3d12` is reserved for the real resource-backed D3D12 backend. It currently fails closed:

```text
D3D12 real replay unavailable: capture/replay now includes bounded index snapshots for fresh captures, but still does not include vertex fetch constants, vertex-buffer bytes, translated input layouts, textures/samplers, render-target/depth state, or replacement BO2 shaders.
```

This behavior is intentional. The real backend must not emit synthetic geometry through the real backend name.

## Current verified blocker

Draw `1209` in `native_captures\payload_capture_002` has enough packet data and index bytes to identify and decode a real indexed draw, but not enough vertex/shader/resource data to submit it:

- Real: draw packet, index metadata, raw index bytes, decoded indices `3,0,2,2,0,1`, shader hashes, and two constant ranges with payload.
- Missing: vertex/fetch decode, raw vertex bytes, shader replacements/translation, render target/depth state, and texture/sampler state.

## Next implementation targets

1. Decode/capture vertex fetch constants and raw vertex bytes for draw `1209`.
2. Add sidecar resource manifests to keep large buffers/textures out of JSONL.
3. Build D3D12 index/vertex buffer creation after replay can load both captured index and vertex data.
4. Add shader runtime-hash matching and manual overrides for the `5D918D91043B3ED0` / `C4ED2979F29C9139` pair.
5. Decode texture/sampler and render-target/depth/blend/raster state for the same frame.
