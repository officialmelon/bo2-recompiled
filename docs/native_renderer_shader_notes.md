# Native Renderer Shader Notes

Evidence date: 2026-06-28

## Existing shader work

The repo contains shader/reference work under `shader_work/`:

- `shader_work/fastfile_dat/`: decoded zone `.dat` files and zone-source metadata.
- `shader_work/shaders/containers/`: extracted shader container blobs.
- `shader_work/shaders/microcode/`: extracted unique shader microcode.
- `shader_work/shaders/index.json` and `index.csv`: occurrence and hash index.
- `shader_work/cache-test/`: sample ReXGlue `.xsh` and `.xpso` cache outputs for title ID `415608C3`.

Current `shader_work/shaders/index.json` summary:

- Zones scanned: `142`
- Shader occurrences: `36,769`
- Unique containers: `33,950`
- Pixel containers: `31,134`
- Vertex containers: `2,816`
- Unique microcode programs: `33,355`
- Pixel microcode programs: `30,740`
- Vertex microcode programs: `2,615`
- Container bytes: `103,924,988`
- Microcode bytes: `54,759,132`
- Invalid bounds: `0`

## Original format

The extractor identifies Xbox 360 shader containers by a big-endian header signature beginning with `0x10 0x2A 0x11`. Valid containers have flags matching `0x102A1100` in the high bits. Bit 0 selects stage:

- `0`: pixel shader
- `1`: vertex shader

The extractor reads virtual and physical sizes from the container header, then reads the embedded microcode pointer at header offset `24`. Microcode SHA-256 hashes are stage-prefixed so identical bytes in different stages stay distinct.

## Game references

`index.csv` records every source zone and byte offset for each unique container. Example rows show entries such as:

- `so_rts_mp_socotra@0x14edab0`
- `angola_2@0x2cf79ac`
- `mp_nightclub@0x7ff1c4`

This makes the fastfile source location the current best static reference back to game assets. Runtime material-pass mapping still needs to connect a material/shader binding to one of these extracted hashes.

## ReXGlue cache format

The existing cache script writes `.xsh` files with:

- Magic `0x48534558`
- Version `0x19122020`
- Entries keyed by XXH3 of raw microcode
- Pixel entries marked with `0x80000000` in the count/type word

`shader_work/cache-test/` contains sample shareable cache files:

- `sp/shaders/shareable/415608C3.xsh`
- `sp/shaders/shareable/415608C3.rov.d3d12.xpso`
- `mp/shaders/shareable/415608C3.xsh`
- `mp/shaders/shareable/415608C3.rov.d3d12.xpso`

## Current blocker

Full automatic native shader replacement is not implemented yet. The extracted microcode is structurally valid, but native pipeline variants require live render state. Existing `SHADERS.md` notes that directly seeding all extracted shaders into ReXGlue is unsafe and has hit unsupported Xenos operations such as export register `48`.

## Native renderer path forward

1. Log material pass/shader binding at runtime once the XEX material load and draw-call functions are mapped.
2. Record the shader microcode hash, stage, material name or asset pointer, sampler bindings, and constant-buffer layout.
3. Add a manual replacement table keyed by stage plus hash.
4. Let the null/debug backend report missing shader mappings before a real backend attempts to draw.
5. Prefer a backend-neutral shader IR or metadata layer so D3D12, Vulkan, Metal, and deko3d can share the same mapping database.
