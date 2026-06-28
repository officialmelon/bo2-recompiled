# Native Renderer Resource Translation

Last updated: 2026-06-29

## Current replay resource coverage

The replay stream currently has real BO2 PM4 draw, shader, constant, swap, and present metadata. Fresh captures now include bounded raw index-buffer snapshots for indexed draws, per-draw vertex fetch constants decoded from the active vertex shader, and bounded raw vertex-buffer snapshots. They do not yet have texture/sampler, render-target/depth, or shader-translation data needed for a real native scene draw.

Verified draw `1209` from `native_captures\payload_capture_002\events.jsonl` is the first useful indexed draw after constants:

- Packet: `PM4_DRAW_INDX`, `packet_ptr=0x0501B6E0`
- Indices: `index_count=6`, `index_base=0x0501E090`, `index_len=12`, `index_format=0`, `endian=1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- Shaders: VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`
- Constants: two ALU ranges at indices `1008` and `2032`, both with payload
- Missing: decoded native input-layout conversion, texture/sampler state, render/depth/blend/raster state, and replacement shaders

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

## Implemented in this pass

- Capture/replay data structs can carry bounded constant payload dwords.
- Old captures remain readable and are explicitly reported as `payload=missing`.
- ReXGlue CP trace draw events now carry up to `1024` raw index bytes per indexed draw, with explicit missing/truncated flags.
- BO2 JSONL capture writes `index_payload_byte_count`, `index_payload_missing`, `index_payload_truncated`, and `index_bytes`.
- Replay validates indexed draws with missing snapshots and decodes captured 16-bit/32-bit index bytes with Xenos endian handling.
- Replay command `--draw <n> --dump-bound-state` prints shader hashes and current bound constant ranges.
- Replay command `--draw <n> --dump-indices` reports real indexed-draw metadata, raw bytes, and decoded indices when snapshots exist.
- ReXGlue CP trace draw events now carry active vertex-shader fetch bindings, raw vertex fetch constant words, decoded address/size/endian/stride fields, decoded shader attribute descriptors, and up to `512` raw vertex bytes per fetch binding.
- BO2 JSONL capture writes `vertex_fetch_count`, `vertex_fetch_truncated`, and structured `vertex_fetches`.
- Replay parses nested fetch/attribute records, validates whether fresh captures carry vertex-fetch state fields, and reports zero-fetch draws separately from old captures with missing fields.
- Replay command `--draw <n> --dump-vertices` now reports real fetch constants, stream addresses, stride, attribute formats, and raw vertex byte previews when snapshots exist.
- Replay vertex dumps now decode captured vertex payloads into CPU-visible float components for observed Xenos formats `6`, `7`, `16`, `17`, `25`, `26`, `31`, `32`, `33`, `34`, `35`, `36`, `37`, `38`, and `57`, with captured endian handling.
- Replay command `--resource-summary` reports snapshot coverage and current real-backend blockers.

## Required next capture fields

1. Native input-layout conversion and backend vertex-buffer packing for the decoded Xenos formats.
2. Textures/samplers: texture fetch constants, base address, dimensions, format, mip count, tiling/swizzle, endian, sampler filter/wrap/lod, and raw bytes or sidecar snapshot.
3. Render state: color/depth target addresses, formats, pitch, viewport, scissor, blend, depth/stencil, rasterizer/cull, clears, resolves, and present target mapping.
4. Shader microcode: raw PM4-loaded microcode bytes or stable sidecar resource references for runtime-used shaders.
5. Sidecar resource manifests for larger vertex, texture, and render-target snapshots so JSONL stays bounded.

## Hard blocker

The current verified capture can replay real index values and decode bounded vertex-buffer bytes for the first useful indexed draws, but it still cannot produce real BO2 scene output because it lacks backend input-layout conversion, shaders, textures/samplers, and render-target/depth/blend/raster state. Any real backend draw would still have to synthesize those pieces, which is intentionally rejected by `--backend d3d12`.
