# Native Renderer Real Backend Status

Last updated: 2026-06-29

## Status

Real native rendering is not complete.

The only working GPU-output backend is `d3d12-diagnostic`, which renders synthetic debug rectangles from captured BO2 events. It is useful for device, command-list, readback, and replay-state validation, but it is not BO2 scene rendering.

## Real D3D12 gate

`native_render_replay.exe --backend d3d12` is reserved for the real resource-backed D3D12 backend. It currently fails closed:

```text
D3D12 real replay unavailable: capture/replay now has bounded index and vertex snapshots where the command stream provides them, but d3d12-real still lacks translated input layouts, native shader replacements/translations, texture/sampler state, and render-target/depth state.
```

This behavior is intentional. The real backend must not emit synthetic geometry through the real backend name.

## Current verified blocker

Draw `1004` in `native_captures\vertex_fetch_capture_001` has enough packet data, index bytes, constants, vertex fetch metadata, and bounded vertex bytes to identify a real indexed draw:

- Real: draw packet, index metadata, raw index bytes, decoded indices `3,0,2,2,0,1`, shader hashes, two constant ranges with payload, `vf95` at guest physical `0x05008230`, stride `32`, attributes with Xenos formats `38`, `6`, and `37`, `128/128` raw vertex bytes, and decoded position/color/UV components.
- Missing: D3D12 input-layout/buffer binding for the decoded vertices, shader replacements/translation, render target/depth state, and texture/sampler state.

## Next implementation targets

1. Build D3D12 index/vertex buffer creation from the captured draw `1004` data.
2. Add native input-layout metadata from replay-decoded Xenos fetch attributes.
3. Add shader runtime-hash matching and manual overrides for the `5D918D91043B3ED0` / `C4ED2979F29C9139` pair.
4. Decode texture/sampler and render-target/depth/blend/raster state for the same frame.
5. Add sidecar resource manifests to keep larger buffers/textures out of JSONL.
