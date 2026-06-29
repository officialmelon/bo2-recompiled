# Native Renderer Real Backend Status

Last updated: 2026-06-29

## Status

Real native rendering is not complete.

The working GPU-output backends are:

- `d3d12-diagnostic`, which renders synthetic debug rectangles from captured BO2 events.
- `d3d12`, which now renders the first resource-backed captured BO2 indexed draw from replay data using real captured vertex and index buffers with either a matching manual HLSL override pair or the explicitly requested temporary diagnostic shader fallback.

Neither path is full BO2 scene rendering yet. The `d3d12` path proves native D3D12 vertex/index buffer binding, manual override shader compilation, and `DrawIndexedInstanced` with captured BO2 geometry, but it still lacks automatic translated BO2 shaders, captured constant-buffer binding, textures/samplers, render-target/depth state, and full-frame state replay. Without a matching override, translated shader, or cache entry, real `d3d12` fails closed.

## Real D3D12 gate

`native_render_replay.exe --backend d3d12` is reserved for the real resource-backed D3D12 backend. It renders only when a translated shader, compiled cache entry, or matching manual override is available. It refuses to render with the temporary shader unless `--allow-diagnostic-shader` is passed.

The fail-closed behavior is verified below with an intentionally empty override root.

Draw `1209` now has a hash-keyed manual D3D12 override pair:

- `shader_work\native_overrides\d3d12\vs_5D918D91043B3ED0.hlsl`
- `shader_work\native_overrides\d3d12\ps_C4ED2979F29C9139.hlsl`
- Manifest: `shader_work\native_overrides\overrides.json`

Verified command:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-manual-override.bmp --no-summary
```

- Exit code: `0`
- Output: `native-renderer-d3d12-real-draw1209-manual-override.bmp`
- SHA-256: `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Shader status: manual HLSL override compiled by the replay backend, not automatic Xenos translation and not a persistent DXIL cache entry yet.

Strict missing-override path:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --no-summary
```

- Exit code: `1`
- Expected message includes `VS=0x5D918D91043B3ED0`, `PS=0xC4ED2979F29C9139`, and the selected override root.

The explicit diagnostic fallback still succeeds for captures with a supported indexed draw snapshot:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --allow-diagnostic-shader --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp --no-summary
```

- Output: `native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp`
- SHA-256: `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`

The rendered geometry is the captured draw `1209` full-screen quad. The visible gradient comes from a diagnostic native shader using captured color/UV attributes, so this output is not shader-correct BO2 rendering.

## Current verified blocker

Draw `1209` in `native_captures\shader_payload_capture_001` has enough packet data, index bytes, constants, shader payloads, vertex fetch metadata, and bounded vertex bytes to identify a real indexed draw:

- Real: draw packet, index metadata, raw index bytes, decoded indices `3,0,2,2,0,1`, shader hashes and uploaded shader payload dwords, two constant ranges with payload, `vf95` at guest physical `0x0501E030`, stride `32`, attributes with Xenos formats `38`, `6`, and `37`, `128/128` raw vertex bytes, and decoded position/color/UV components.
- Implemented: D3D12 canonical input layout, upload-buffer vertex/index resources, and `DrawIndexedInstanced` for this draw.
- Implemented for draw `1209`: hash-keyed manual override lookup, HLSL compilation, canonical D3D12 input layout, upload-buffer vertex/index resources, and `DrawIndexedInstanced`.
- Missing: automatic shader translation/cache, captured constant-buffer binding, render target/depth state, texture/sampler state, and full-frame multi-draw replay.

## Next implementation targets

1. Promote `shader_work\native_overrides\overrides.json` from documentation to a parsed override table and emit cache records for compiled overrides.
2. Add constant-buffer binding from captured payloads.
3. Decode texture/sampler and render-target/depth/blend/raster state for the same frame.
4. Expand D3D12 real replay from one selected draw to all supported draws in the captured frame.
5. Add sidecar resource manifests to keep larger buffers/textures out of JSONL.
