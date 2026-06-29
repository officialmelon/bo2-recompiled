# Native Renderer Shader Notes

Evidence date: 2026-06-29

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

## Runtime replay shader evidence

`native_render_replay.exe` was run on `native-renderer-capture-limit.jsonl` on 2026-06-28. The capture contains `1011` shader-load events and `4388` draw events. Replay found `8` unique live shader hashes and `7` live shader pairs, with no draw missing a VS or PS hash.

Top live shader hashes:

| Stage | Runtime hash | Draws | Loads | Max dwords |
|---|---|---:|---:|---:|
| PS | `0xA4A965C189287B99` | 4268 | 403 | 9 |
| VS | `0xB6C9863F710683EC` | 4008 | 167 | 24 |
| VS | `0x1E6883FCCDE1F688` | 235 | 236 | 27 |
| PS | `0x246E20EF10E0DDC7` | 100 | 50 | 117 |
| VS | `0xAB1E86137A0240E8` | 85 | 85 | 15 |
| VS | `0x81311AC4B1FBD082` | 50 | 50 | 39 |
| PS | `0xC4ED2979F29C9139` | 20 | 10 | 72 |
| VS | `0x5D918D91043B3ED0` | 10 | 10 | 63 |

Top live shader pairs:

| VS | PS | Draws |
|---|---|---:|
| `0xB6C9863F710683EC` | `0xA4A965C189287B99` | 4008 |
| `0x1E6883FCCDE1F688` | `0xA4A965C189287B99` | 235 |
| `0x81311AC4B1FBD082` | `0x246E20EF10E0DDC7` | 50 |
| `0xAB1E86137A0240E8` | `0x246E20EF10E0DDC7` | 50 |
| `0xAB1E86137A0240E8` | `0xA4A965C189287B99` | 25 |
| `0x5D918D91043B3ED0` | `0xC4ED2979F29C9139` | 10 |
| `0xAB1E86137A0240E8` | `0xC4ED2979F29C9139` | 10 |

These runtime hashes are not the same IDs as `shader_work/shaders/index.json` container SHA-256 hashes. They come from the ReXGlue command-processor trace over loaded Xenos microcode. The replacement registry should therefore key first on `(stage, runtime_hash)` and later attach source container/microcode metadata when the hash relationship is proven.

## Updated runtime shader evidence

`native_shader_inspect.exe` now reads native renderer captures directly:

```powershell
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --list-runtime-shaders --top-shaders 20
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --match-runtime-shaders --top-shaders 20
```

Verified capture `vertex_fetch_capture_001`:

- Lines: `12000`
- Shader events: `594`
- Draw events: `2646`
- Unique runtime shaders: `8`
- Runtime shader pairs: `7`

Verified capture `shader_payload_capture_001` after adding PM4 shader payload capture:

- Lines: `12000`
- Shader events: `587`
- Draw events: `2654`
- Unique runtime shaders: `8`
- Runtime shader pairs: `7`
- Shader payload coverage: `587/587`, `13230` payload dwords, no missing or truncated shader payloads
- Target draw `1209`: VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`, both with captured PM4 shader payloads
- Direct static-index matching remains `0/8`
- `native_shader_inspect.exe` computes raw little-endian, raw big-endian, trailing-zero-trimmed little-endian, and trailing-zero-trimmed big-endian SHA-256 values for captured PM4 payload dwords. All `8` runtime shaders in this capture have stable payload hashes across repeated uploads, but all four payload-hash rules still report no `shader_work/shaders/index.json` match.

Current shader identity conclusion: runtime 64-bit IDs and directly hashed PM4 upload payloads are not enough to map to extracted container or microcode hashes. The next evidence target is shader record/material metadata from the XEX/PC PDB path, plus aligned/padded payload hash variants and explicit override mapping.

Top runtime shader pairs in `vertex_fetch_capture_001`:

| VS | PS | Draws |
|---|---|---:|
| `0xB6C9863F710683EC` | `0xA4A965C189287B99` | 2424 |
| `0x1E6883FCCDE1F688` | `0xA4A965C189287B99` | 139 |
| `0xAB1E86137A0240E8` | `0xA4A965C189287B99` | 21 |
| `0x81311AC4B1FBD082` | `0x246E20EF10E0DDC7` | 20 |
| `0xAB1E86137A0240E8` | `0x246E20EF10E0DDC7` | 20 |
| `0x5D918D91043B3ED0` | `0xC4ED2979F29C9139` | 11 |
| `0xAB1E86137A0240E8` | `0xC4ED2979F29C9139` | 11 |

Direct static-index matching on `vertex_fetch_capture_001`:

- Exact substring matches in `shader_work/shaders/index.json`: `0/8`
- Draw `1004` pair `VS=0x5D918D91043B3ED0`, `PS=0xC4ED2979F29C9139`: no direct match to static container or microcode hashes.

Ghidra PDB evidence:

- `Material_RegisterVertexShader` and `Material_RegisterPixelShader` use material-loader shader hash tables and load by shader name when the hash table misses.
- `Material_LoadPassVertexShader` and `Material_LoadPassPixelShader` parse `vertexShader` / `pixelShader` tokens, then call `Material_SetPassShaderArguments_DX`.
- `Material_SetPassShaderArguments_DX` reflects shader bytecode and records input/output/resource argument metadata.

Inference: the runtime 64-bit PM4 hashes in the capture are not proven static SHA-256 container or microcode hashes. Raw PM4 shader payload bytes are now captured for the tested runtime shaders, and exact/byte-swapped/trailing-zero-trimmed payload hashes still do not match the static index. The next matching rule must capture material shader-record metadata, compare aligned/padded payload variants, and use material name/hash-table records or explicit overrides.

## Shader inspection tool

`native_shader_inspect.exe` has been added as the first static shader registry inspection tool. Verified command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_renderer_direct_build\native_shader_inspect_direct.exe --index shader_work\shaders\index.json --summary
```

It loads `shader_work/shaders/index.json`, prints the summary counts, previews the first pixel and vertex containers, supports `--hash <value> --find` for static container/microcode substring search, and ranks/matches runtime capture hashes with `--capture`, `--list-runtime-shaders`, and `--match-runtime-shaders`.

## Native renderer path forward

1. Preserve the runtime `(stage, hash, guest_address, dword_count)` stream from replay as the first shader registry key.
2. Extend capture with material/pass identity, sampler bindings, texture fetch constants, and full constant payload bytes.
3. Map runtime hashes back to `shader_work` container/microcode records where possible.
4. Add a manual replacement table keyed by stage plus runtime hash.
5. Let the null/debug and replay backends report missing shader mappings before a real backend attempts to draw.
6. Prefer a backend-neutral shader IR or metadata layer so D3D12, Vulkan, Metal, and deko3d can share the same mapping database.
