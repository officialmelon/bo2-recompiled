# Native Renderer Resource Translation

Last updated: 2026-06-28

## Current replay resource coverage

The replay stream currently has real BO2 PM4 draw, shader, constant, swap, and present metadata. It does not yet have the resource byte snapshots needed for a real native draw.

Verified draw `1209` from `native-renderer-capture-limit.jsonl` is the first useful indexed draw after constants:

- Packet: `PM4_DRAW_INDX`, `packet_ptr=0x0501B4F0`
- Indices: `index_count=6`, `index_base=0x0501E0A0`, `index_len=12`, `index_format=0`, `endian=1`
- Shaders: VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`
- Constants: two ALU ranges at indices `1008` and `2032`
- Missing: raw index bytes, vertex/fetch constants, vertex bytes, texture/sampler state, render/depth/blend/raster state, and replacement shaders

## Implemented in this pass

- Capture/replay data structs can carry bounded constant payload dwords.
- Old captures remain readable and are explicitly reported as `payload=missing`.
- Replay command `--draw <n> --dump-bound-state` prints shader hashes and current bound constant ranges.
- Replay command `--draw <n> --dump-indices` reports real indexed-draw metadata and whether raw index bytes exist.
- Replay command `--draw <n> --dump-vertices` reports current fetch/vertex coverage.
- Replay command `--resource-summary` reports snapshot coverage and current real-backend blockers.

## Required next capture fields

1. Index buffers: guest/physical address, byte length, 16/32-bit format, endian mode, and raw bytes.
2. Vertex/fetch constants: raw fetch words, stream base, stride, attribute offset, format, endian/swap, and raw vertex bytes.
3. Textures/samplers: texture fetch constants, base address, dimensions, format, mip count, tiling/swizzle, endian, sampler filter/wrap/lod, and raw bytes or sidecar snapshot.
4. Render state: color/depth target addresses, formats, pitch, viewport, scissor, blend, depth/stencil, rasterizer/cull, clears, resolves, and present target mapping.
5. Shader microcode: raw PM4-loaded microcode bytes or stable sidecar resource references for runtime-used shaders.

## Hard blocker

The current verified capture cannot produce real BO2 geometry because it has no index-buffer or vertex-buffer byte snapshots. Any real backend draw would have to synthesize resources, which is intentionally rejected by `--backend d3d12`.
