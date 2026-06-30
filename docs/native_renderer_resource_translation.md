# Native Renderer Resource Translation

Last updated: 2026-06-30

## Current replay resource coverage

The replay stream currently has real BO2 PM4 draw, shader, constant, swap, and present metadata. Fresh captures now include bounded raw index-buffer snapshots for indexed draws, per-draw vertex fetch constants decoded from the active vertex shader, bounded raw vertex-buffer snapshots, texture fetch constants with inline previews plus sidecar payload resources for larger texture snapshots, and decoded render-state register snapshots. They do not yet have complete texture format/mip coverage, render-target/depth image snapshots, or shader-translation data needed for a real native scene draw.

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
- D3D12 real replay now consumes captured render state when creating replay PSOs: rasterizer culling/front-face/fill/depth-clip bits, color write mask, blend factors/ops, depth/stencil state, and bounded screen scissor. The real replay path now creates and binds an offscreen `D32_FLOAT` DSV so depth-enabled PSOs have a depth target. Captured BO2 depth contents are still missing because target/depth readback sidecars are zero in the tested captures.
- D3D12 real replay now carries a decoded input-layout mask from vertex fetches into the PSO key. The canonical `POSITION/COLOR0/TEXCOORD0` layout remains present for current translated shaders, and captured layouts can add `NORMAL0` and `TEXCOORD1`. Texture conversion now supports format `26` (`16_16_16_16`) and format `38` (`32_32_32_32_FLOAT`) as RGBA8 preview/upload sources in addition to formats `2` and `6`.
- Real replay no longer silently replaces unsupported captured texture fetches with white textures. Captured-but-undecodable texture state fails strict real replay, while frame skip mode records the unsupported texture reason and skips the draw. Draws with no captured texture fetch still bind inert descriptors to satisfy the current root table.
- Texture payloads larger than `256` bytes are now written to sidecar files under `native_captures\<capture>\resources` and referenced from JSONL by `payload_resource_path` and `payload_resource_byte_count`. Replay loads the sidecar before summary/validation/backend upload, while the JSONL keeps a 64-byte preview for inspection. Captures also write both `resources\index.json` and a line-oriented `resources\index.jsonl`; the writer finalizes both when the capture event limit is reached.
- Fresh sidecar capture `native_captures\sidecar_capture_002\events.jsonl` validates `12000` events, `52` frames, and `2628` draws. It has `412` texture fetch records, `372` sidecar texture payloads, `3999744` sidecar bytes, `4004864` replay-loaded texture payload bytes, and no missing texture snapshots.
- PM4 swap/frontbuffer payloads now use the same sidecar system. ReXGlue reads the frontbuffer pointer and dimensions from `PM4_XE_SWAP`, computes `width * height * 4`, copies up to an 8 MiB synchronous payload, and BO2 writes it as `frontbuffer_payload` sidecars referenced by `frontbuffer_payload_resource_path`.
- Fresh frontbuffer capture `native_captures\frontbuffer_capture_001\events.jsonl` validates `3000` events, `16` frames, and `673` draws. Resource summary reports `frontbuffer_snapshots=15`, `payload_bytes=55296000`, `sidecars=15`, `sidecar_bytes=55296000`, `truncated=0`, with `15` manifest entries of `3686400` bytes each.
- The tested frontbuffer payloads are all zero in the first observed swaps. This means present memory is readable and manifest-backed, but it is not yet the render-target/depth data needed for native scene reconstruction. The next resource snapshot should target `render_state.color_base[] << 12` and `depth_base << 12` around useful draws, with deduplication or strict caps to avoid per-draw megabyte explosions.
- Draw-time color/depth target payload previews now exist. ReXGlue samples a bounded 16 KiB window from `color_base << 12` and `depth_base << 12` for each draw with target bases, records the full requested footprint, records `payload_offset_bytes`, and BO2 stores those previews as `color_target_payload` / `depth_target_payload` sidecars.
- Fresh target snapshot capture `native_captures\target_snapshot_capture_003\events.jsonl` validates `1800` events, `10` frames, and `395` draws. Resource summary reports `color_target_snapshots=368`, `color_target_payload_bytes=6029312`, `depth_target_snapshots=369`, `depth_target_payload_bytes=6045696`, all sidecar-backed and truncated because each entry is a 16 KiB preview.
- Draw `29` in `target_snapshot_capture_003` shows the corrected `1280x720x4` target footprint: color target base `1328`, depth base `608`, `payload_requested_byte_count=3686400`, `payload_offset_bytes=1843200`, and `payload=16384/3686400 sidecar truncated`.
- Scanning all color/depth target sidecars in `target_snapshot_capture_003` found only zero bytes. This proves the CPU guest-memory path can read those target ranges, but the CPU memory mirror is not receiving GPU-rendered target contents at draw time. The next reverse-engineering target is the ReXGlue/Xenos GPU render-target backing/readback or resolve path, not another CPU-memory copy from the same base addresses.
- ReXGlue's existing resolve readback path is useful for capture when enabled. Running with `--readback_resolve full` produced `native_captures\resolve_readback_capture_001\events.jsonl`, which validates `3000` events, `16` frames, and `671` draws. In that capture, `13/15` PM4 swap frontbuffer sidecars contain nonzero bytes and `9/15` contain nonzero RGB pixels.
- Swap capture now records texture fetch constant `0` metadata on `pm4_swap` events. The frontbuffer payload request uses the Xenos tiled upper-bound footprint instead of only `width * height * 4`, which is required because a `1280x720` tiled format-6 swap texture has a `3768320` byte footprint even though the visible linear image is `3686400` bytes.
- Fresh swap-fetch capture `native_captures\swap_fetch_capture_003\events.jsonl` validates `3000` events, `16` frames, and `670` draws. Resource summary reports `frontbuffer_snapshots=15`, `payload_bytes=56524800`, `sidecars=15`, `sidecar_bytes=56524800`, and `truncated=0`.
- Replay command `--dump-frontbuffer` now decodes complete format-6 tiled frontbuffer payloads with captured fetch0 endian/swizzle metadata. On `swap_fetch_capture_003`, snapshot `3` (`seq=508`, `frontbuffer=0x1DD38000`) decodes with `decode_mode=fetch0_tiled_rgba8`, `format=6`, `endian=0`, `tiled=yes`, `pitch=40`, `swizzle=0x00000A0A`; output SHA-256 is `CD6890DFB492C3D8124A3E41DBF1161B2AD3821A6CE365F8509775D2F42A414B`.
- Visual status: the decoded frontbuffer is coherent but the tested early frame is solid blue. This proves the frontbuffer texture decode path; it is not full BO2 scene output and does not replace the need for real draw sequencing, shader translation, render target/depth state, and live native backend integration.

## Required next capture fields

1. Expand sidecar-backed texture snapshots beyond the current bounded base payloads: mip footprints and conversion beyond format `6` to the next runtime-used formats.
2. Render-target/depth resource snapshots and resolve/frontbuffer mapping for scene frames, not only early solid-color frontbuffer swaps.
3. Shader identity metadata: material/shader record pointers, stripped/aligned payload hashes, and source container/microcode mapping evidence.
4. Shader microcode: raw PM4-loaded microcode bytes or stable sidecar resource references for runtime-used shaders.

## Hard blocker

The current verified capture can replay real index values, decode bounded vertex-buffer bytes, list texture fetches, and decode render-state registers for the first useful indexed draws. `--backend d3d12` can now bind a canonical D3D12 vertex/index layout for the supported shader pair and issue `DrawIndexedInstanced` from captured BO2 data.

It still cannot produce real BO2 scene output because it lacks automatic translated BO2 shaders for all runtime pairs, complete Xenos texture format/mip coverage, nonzero captured render-target/depth resource snapshots, and corrected full-frame state sequencing. The visible D3D12 real output is therefore captured BO2 geometry through the current translated/override subset, not shader-correct full-scene rendering.
