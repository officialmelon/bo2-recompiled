# Native Renderer Real Backend Status

Last updated: 2026-06-28

## Status

Real native rendering is not complete.

The only working GPU-output backend is `d3d12-diagnostic`, which renders synthetic debug rectangles from captured BO2 events. It is useful for device, command-list, readback, and replay-state validation, but it is not BO2 scene rendering.

## Real D3D12 gate

`native_render_replay.exe --backend d3d12` is reserved for the real resource-backed D3D12 backend. It currently fails closed:

```text
D3D12 real replay unavailable: capture/replay does not yet include real index-buffer bytes, vertex fetch constants, vertex-buffer bytes, translated input layouts, or replacement BO2 shaders.
```

This behavior is intentional. The real backend must not emit synthetic geometry through the real backend name.

## Current verified blocker

Draw `1209` has enough packet metadata to identify a real indexed draw, but not enough resource data to submit it:

- Real: draw packet, index metadata, shader hashes, and constant range metadata.
- Missing: raw index bytes, vertex/fetch decode, raw vertex bytes, shader replacements/translation, render target/depth state, and texture/sampler state.

## Next implementation targets

1. Compile a runtime with ReXGlue CP trace fields for bounded constant payloads.
2. Capture a new JSONL and verify `constant_uploads_with_payload > 0`.
3. Extend the CP trace sink with index-buffer and vertex/fetch snapshots.
4. Add sidecar resource manifests to keep large buffers/textures out of JSONL.
5. Build the first D3D12 real path only after replay can load actual index and vertex data for draw `1209` or another selected draw.
