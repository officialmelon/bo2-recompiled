# Native Renderer Resource Translation

Last updated: 2026-06-29

## Current replay resource coverage

The replay stream currently has real BO2 PM4 draw, shader, constant, swap, and present metadata. Fresh captures now include bounded raw index-buffer snapshots for indexed draws, per-draw vertex fetch constants decoded from the active vertex shader, bounded raw vertex-buffer snapshots, texture fetch constants with bounded texture payload prefixes, and decoded render-state register snapshots. They do not yet have sidecar resources, complete texture untile/format conversion, render-target/depth image snapshots, or shader-translation data needed for a real native scene draw.

Verified draw `1209` from `native_captures\payload_capture_002\events.jsonl` is the first useful indexed draw after constants:

- Packet: `PM4_DRAW_INDX`, `packet_ptr=0x0501B6E0`
- Indices: `index_count=6`, `index_base=0x0501E090`, `index_len=12`, `index_format=0`, `endian=1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- Shaders: VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`
- Constants: two ALU ranges at indices `1008` and `2032`, both with payload
- Missing: decoded native input-layout conversion in older captures, complete texture format/mip conversion, render/depth/blend/raster state application, and replacement shaders

Verified draw `1004` from `native_captures\vertex_fetch_capture_001\events.jsonl` is the first useful indexed draw with both index and vertex payloads:

- Packet: `PM4_DRAW_INDX`, `packet_ptr=0x05005900`
- Indices: `index_count=6`, `index_base=0x050082B0`, `index_len=12`, `index_format=0`, `endian=1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- Vertex fetch: `vf95`, raw words `0x05008233 0x10000082`, address `0x05008230`, size `128` bytes, stride `32` bytes, endian `2`
- Attributes: format `38` at offset `0`, format `6` at offset `16`, format `37` at offset `20`
- Raw vertex payload: `128/128` bytes captured
- Decoded vertices:
  - `v0`: position `(0,0,0,1)`, color `(1,1,1,1)`, uv `(0,0)`
  - `v1`: position `(1280,0,0,1)`, color `(1,1,1,1)`, uv `(1,0)`
  - `v2`: position `(1280,720,0,1)`, color `(1,1,1,1)`, uv `(1,1)`
  - `v3`: position `(0,720,0,1)`, color `(1,1,1,1)`, uv `(0,1)`
- Shaders: VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`
- Constants: two ALU ranges at indices `1008` and `2032`, both with payload

Verified draw `799` from `native_captures\state_capture_004\events.jsonl` is the first useful indexed draw with index, vertex, texture, constant, and render-state payloads in one record:

- Packet: `PM4_DRAW_INDX`, `packet_ptr=0x04FEF930`
- Indices: `index_count=6`, `index_base=0x04FF24E0`, `index_len=12`, `index_format=0`, `endian=1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- Vertex fetch: `vf95`, address `0x04FF2460`, size `128` bytes, stride `32` bytes, endian `2`, formats `38`, `6`, and `37`
- Texture fetches: four pixel-shader bindings with raw six-dword fetch constants, decoded 1x1 tiled format `6` resources at `0x05B67000` and `0x05B6B000`, endian `2`, and `4096` captured payload bytes per binding
- Constants: two ALU ranges at indices `1008` and `2032`, both with payload
- Render state: `surface_pitch=1280`, `depth_base=608`, `depth_format=1`, color target base `1328`, color mask `0x0000000F`, depth test/write disabled, stencil disabled, and cull mode `2`
- Missing: complete Xenos texture format/mip conversion beyond 2D format `6`, render-target/depth resource snapshots, full heterogeneous render-state application in D3D12, and shader-correct BO2 shaders

## Implemented in this pass

- Capture/replay data structs can carry bounded constant payload dwords.
- Old captures remain readable and are explicitly reported as `payload=missing`.
- ReXGlue CP trace draw events now carry up to `1024` raw index bytes per indexed draw, with explicit missing/truncated flags.
- BO2 JSONL capture writes `index_payload_byte_count`, `index_payload_missing`, `index_payload_truncated`, and `index_bytes`.
- Replay validates indexed draws with missing snapshots and decodes captured 16-bit/32-bit index bytes with Xenos endian handling.
- Replay command `--draw <n> --dump-bound-state` prints shader hashes and current bound constant ranges.
- Replay summaries now report PM4 shader upload payload coverage. Fresh capture `shader_payload_capture_001` has `587/587` shader uploads with payload and `13230` captured payload dwords.
- Replay command `--draw <n> --dump-indices` reports real indexed-draw metadata, raw bytes, and decoded indices when snapshots exist.
- ReXGlue CP trace draw events now carry active vertex-shader fetch bindings, raw vertex fetch constant words, decoded address/size/endian/stride fields, decoded shader attribute descriptors, and up to `512` raw vertex bytes per fetch binding.
- BO2 JSONL capture writes `vertex_fetch_count`, `vertex_fetch_truncated`, and structured `vertex_fetches`.
- Replay parses nested fetch/attribute records, validates whether fresh captures carry vertex-fetch state fields, and reports zero-fetch draws separately from old captures with missing fields.
- Replay command `--draw <n> --dump-vertices` now reports real fetch constants, stream addresses, stride, attribute formats, and raw vertex byte previews when snapshots exist.
- Replay vertex dumps now decode captured vertex payloads into CPU-visible float components for observed Xenos formats `6`, `7`, `16`, `17`, `25`, `26`, `31`, `32`, `33`, `34`, `35`, `36`, `37`, `38`, and `57`, with captured endian handling.
- Replay command `--resource-summary` reports snapshot coverage and current real-backend blockers.
- Target draw `1209` in `shader_payload_capture_001` has real index bytes, decoded indices `3,0,2,2,0,1`, `vf95` vertex fetch state, `128/128` vertex bytes, decoded position/color/UV vertices, two constant ranges with payload, and captured VS/PS PM4 shader payload dwords.
- ReXGlue CP trace draw events now carry up to `64` texture fetch records from the active VS/PS shader analyzer output. Each record preserves the raw six fetch dwords, decoded base/mip addresses, dimensions, format, tiling, endian, filters, swizzle, dimension, and up to `4096` payload bytes.
- ReXGlue CP trace draw events now also carry Xenos texture clamp modes, and BO2 JSONL capture serializes them as `clamp_x`, `clamp_y`, and `clamp_z`.
- ReXGlue CP trace draw events now carry a decoded render-state snapshot from the current CP register state: RB mode/surface/color/depth controls, color/depth target info, blend controls, viewport registers, scissor/window registers, clip/VTE/SU/SQ state, decoded pitch/MSAA/color/depth/stencil/cull fields, and color/depth target base fields.
- BO2 JSONL capture writes structured `texture_fetches` and `render_state` objects on `pm4_draw` events.
- Replay parses and summarizes texture fetch/resource payload coverage, clamp-mode coverage, and render-state coverage. `clamp_capture_001` validates `344/344` texture fetch records with clamp modes and `1409024` texture payload bytes.
- Replay command `--draw <n> --dump-bound-state` reports texture bindings and render-state basics; draw `799` is the current combined texture/render-state target.
- D3D12 real replay now uploads the first decodable captured texture fetch for each supported draw as an `R8G8B8A8_UNORM` texture, creates an SRV bound at `t0`, and binds a per-draw sampler descriptor at `s0` from captured texture-filter and clamp fields. This is currently implemented for texture format `6` (`k_8_8_8_8`) with Xenos 2D tiled address decoding; unsupported formats or payloads that do not contain the required tiled footprint bind a white fallback SRV/sampler pair and report the count. Old captures without clamp fields continue to use clamp-addressing fallback.
- D3D12 real replay now consumes the first supported draw's captured render state when creating the replay PSO: rasterizer culling/front-face/fill/depth-clip bits, color write mask, blend factors/ops, disabled depth/stencil state, and bounded screen scissor. Depth-enabled draws still need real captured DSV resources before depth test/write can be enabled.

## Required next capture fields

1. Sidecar resource manifests for larger vertex, texture, and render-target snapshots so JSONL stays bounded.
2. Add sidecar/full-payload texture snapshots for larger tiled footprints and expand conversion beyond format `6` to the next runtime-used formats.
3. Render-target/depth resource snapshots and resolve/frontbuffer mapping.
4. Shader identity metadata: material/shader record pointers, stripped/aligned payload hashes, and source container/microcode mapping evidence.
5. Shader microcode: raw PM4-loaded microcode bytes or stable sidecar resource references for runtime-used shaders.

## Hard blocker

The current verified capture can replay real index values, decode bounded vertex-buffer bytes, list texture fetches, and decode render-state registers for the first useful indexed draws. `--backend d3d12` can now bind a canonical D3D12 vertex/index layout for the supported shader pair and issue `DrawIndexedInstanced` from captured BO2 data.

It still cannot produce real BO2 scene output because it lacks automatic translated BO2 shaders, complete Xenos texture format/mip coverage, render-target/depth resource snapshots, real DSV binding for depth-enabled draws, per-state PSO switching, and full-frame state sequencing. The visible D3D12 real output is therefore captured BO2 geometry with a manual override or explicit diagnostic shader path, not shader-correct BO2 rendering.
