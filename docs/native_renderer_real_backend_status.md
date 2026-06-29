# Native Renderer Real Backend Status

Last updated: 2026-06-29

## Status

Real native rendering is not complete.

The working GPU-output backends are:

- `d3d12-diagnostic`, which renders synthetic debug rectangles from captured BO2 events.
- `d3d12`, which now renders the first resource-backed captured BO2 indexed draw from replay data using real captured vertex and index buffers with either a matching manual HLSL override pair or the explicitly requested temporary diagnostic shader fallback.

Neither path is full BO2 scene rendering yet. The `d3d12` path proves native D3D12 vertex/index buffer binding, manual override shader manifest parsing, compiled shader-cache hits, flattened captured constant root binding, and `DrawIndexedInstanced` with captured BO2 geometry, but it still lacks automatic translated BO2 shaders, full constant-layout reconstruction, textures/samplers, render-target/depth state, and full-frame state replay. Without a matching cache entry, override, or translated shader, real `d3d12` fails closed.

## Real D3D12 gate

`native_render_replay.exe --backend d3d12` is reserved for the real resource-backed D3D12 backend. It renders only when a translated shader, compiled cache entry, or matching manual override is available. It refuses to render with the temporary shader unless `--allow-diagnostic-shader` is passed.

The fail-closed behavior is verified below with an intentionally empty override root.

Draw `1209` now has a hash-keyed manual D3D12 override pair:

- `shader_work\native_overrides\d3d12\vs_5D918D91043B3ED0.hlsl`
- `shader_work\native_overrides\d3d12\ps_C4ED2979F29C9139.hlsl`
- Manifest: `shader_work\native_overrides\overrides.json`

Verified command:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-constant-bound.bmp --no-summary
```

- Exit code: `0`
- Output: `native-renderer-d3d12-real-draw1209-constant-bound.bmp`
- SHA-256: `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Shader status: manual HLSL override loaded through `overrides.json`, compiled by the replay backend, cached as D3DCompile `.dxbc`, and bound with flattened captured constants at `b1`. This is not automatic Xenos translation and not DXC/DXIL yet.

Cache-only strict command:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-constant-bound-cache-only.bmp --no-summary
```

- Exit code: `0`
- Output SHA-256: `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Evidence: succeeds with an empty override root because `shader_work\cache\shader_cache_index.json` maps the runtime hashes to compiled `.dxbc` blobs.

Supported shader-pair multi-draw command:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --d3d12-draws 64 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-supported-pair-multidraw-log.bmp --no-summary
```

- Exit code: `0`
- Backend log: `D3D12 real replay submitted 11 supported draw(s) for shader pair VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 out of 11 captured draw(s) with that pair`
- Output SHA-256: `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`

Strict missing-cache-and-override path:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_cache --no-summary
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
- Implemented for the draw-1209 shader pair: shader cache-index lookup, manifest override lookup, HLSL compilation/cache write, flattened captured constants at `b1`, canonical D3D12 input layout, upload-buffer vertex/index resources, and `DrawIndexedInstanced` for all `11/11` captured draws with that pair.
- Latest capture-completeness evidence: `state_capture_004` validates `344` texture fetch snapshots, `1409024` texture payload bytes, and render-state records on `2475/2475` draws. Draw `799` carries the first complete packet set seen in replay: real indices, vertex fetch bytes, four texture fetch records, constants, and render state. D3D12 real replay on this capture still only submits the existing manual-override shader pair (`4/4` supported draws) and does not yet bind the captured textures or apply captured render state.
- Missing: automatic Xenos shader translation, DXC/DXIL compilation, full constant-layout reconstruction, render-target/depth resource snapshots, D3D12 texture/sampler binding, D3D12 render-state application, and full-frame multi-draw replay.

## Next implementation targets

1. Bind captured texture fetches in D3D12 for draw `799`, starting with Xenos tiled texture format `6` and the observed pixel-shader sampler state.
2. Apply the captured render state in D3D12: color/depth target descriptors, viewport/scissor, color mask, depth/stencil, cull/raster, and blend state.
3. Replace flattened constant root data with layout-aware constant buffers from shader metadata.
4. Expand D3D12 real replay from one selected draw to all supported draws in the captured frame.
5. Add DXC/DXIL support and reuse the same cache index for translated shaders.
6. Add sidecar resource manifests to keep larger buffers/textures out of JSONL.
