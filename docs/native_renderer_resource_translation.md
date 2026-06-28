# Native Renderer Resource Translation

Last updated: 2026-06-28

## Current replay resource coverage

The replay stream currently has real BO2 PM4 draw, shader, constant, swap, and present metadata. Fresh captures now include bounded raw index-buffer snapshots for indexed draws. They do not yet have vertex/fetch, texture/sampler, render-target/depth, or shader-translation data needed for a real native scene draw.

Verified draw `1209` from `native_captures\payload_capture_002\events.jsonl` is the first useful indexed draw after constants:

- Packet: `PM4_DRAW_INDX`, `packet_ptr=0x0501B6E0`
- Indices: `index_count=6`, `index_base=0x0501E090`, `index_len=12`, `index_format=0`, `endian=1`
- Raw index bytes: `00 03 00 00 00 02 00 02 00 00 00 01`
- Decoded indices: `3,0,2,2,0,1`
- Shaders: VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`
- Constants: two ALU ranges at indices `1008` and `2032`, both with payload
- Missing: vertex/fetch constants, vertex bytes, texture/sampler state, render/depth/blend/raster state, and replacement shaders

## Implemented in this pass

- Capture/replay data structs can carry bounded constant payload dwords.
- Old captures remain readable and are explicitly reported as `payload=missing`.
- ReXGlue CP trace draw events now carry up to `1024` raw index bytes per indexed draw, with explicit missing/truncated flags.
- BO2 JSONL capture writes `index_payload_byte_count`, `index_payload_missing`, `index_payload_truncated`, and `index_bytes`.
- Replay validates indexed draws with missing snapshots and decodes captured 16-bit/32-bit index bytes with Xenos endian handling.
- Replay command `--draw <n> --dump-bound-state` prints shader hashes and current bound constant ranges.
- Replay command `--draw <n> --dump-indices` reports real indexed-draw metadata, raw bytes, and decoded indices when snapshots exist.
- Replay command `--draw <n> --dump-vertices` reports current fetch/vertex coverage.
- Replay command `--resource-summary` reports snapshot coverage and current real-backend blockers.

## Required next capture fields

1. Vertex/fetch constants: raw fetch words, stream base, stride, attribute offset, format, endian/swap, and raw vertex bytes.
2. Textures/samplers: texture fetch constants, base address, dimensions, format, mip count, tiling/swizzle, endian, sampler filter/wrap/lod, and raw bytes or sidecar snapshot.
3. Render state: color/depth target addresses, formats, pitch, viewport, scissor, blend, depth/stencil, rasterizer/cull, clears, resolves, and present target mapping.
4. Shader microcode: raw PM4-loaded microcode bytes or stable sidecar resource references for runtime-used shaders.

## Hard blocker

The current verified capture can replay real index values, but it still cannot produce real BO2 geometry because it has no vertex/fetch layout or vertex-buffer byte snapshots. Any real backend draw would still have to synthesize vertices, input layouts, textures, render state, and shaders, which is intentionally rejected by `--backend d3d12`.
