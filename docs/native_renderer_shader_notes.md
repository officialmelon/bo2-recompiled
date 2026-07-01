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

## Shader file inspection

Evidence date: 2026-06-30

`native_shader_inspect.exe` now has direct file-level inspection commands:

```powershell
native_shader_inspect.exe --shader C:\Users\braxt\bo2-recompiled\shader_work\shaders\containers\pixel_00011f10fef6a0b01806b897b1e93287c2622b233a5ed9f39d8a983d45226ae9.bin --dump-header
native_shader_inspect.exe --shader C:\Users\braxt\bo2-recompiled\shader_work\shaders\containers\vertex_002ee957c754be9d2a671f0dc0b63fca861b2398f5483703ec93f861466bf80d.bin --dump-header
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.ucode --dump-words --limit 16
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\vertex_4cbe8078e63245600ee471c244caa9b01c3943e54859b5ce9cabeb96d961553b.ucode --disassemble --limit 12
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.ucode --write-disasm C:\Users\braxt\bo2-recompiled\shader_work\out\disasm
native_shader_inspect.exe --microcode C:\Users\braxt\bo2-recompiled\shader_work\shaders\microcode\pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.ucode --write-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
```

Verified pixel container header:

- File bytes: `3452`
- Flags: `0x102A1100`, stage guess `pixel`
- Virtual size: `1508`
- Physical size: `1944`
- Header size: `36`
- Name offset: `0x00000090`
- Metadata offset: `0x00000574`
- Microcode descriptor offset: `0x000005A4`
- Shader name: `pimp_shader_sw4_3d_char_skin_tension_5eee5b1e_ps_main_ps_3_0_ee259e07f134def2b4abdec00183c00f.updb`
- Microcode descriptor: `[0]=0x000000C0`, `[1]=0x000006D8`; descriptor size candidate `1752`, matching the tested pixel `.ucode` byte length.

Verified vertex container header:

- File bytes: `2716`
- Flags: `0x102A1101`, stage guess `vertex`
- Virtual size: `1224`
- Physical size: `1492`
- Header size: `36`
- Name offset: `0x00000080`
- Metadata offset: `0x00000444`
- Microcode descriptor offset: `0x0000046C`
- Shader name: `pimp_shader_treecanopy_2e501d62_vs_main_vs_3_0_d9a8545395d8ec777b8591bb6e2feac1.updb`
- Microcode descriptor: `[0]=0x00000040`, `[1]=0x00000594`; descriptor size candidate `1428`, matching the tested vertex `.ucode` byte length.

The microcode commands print big-endian dwords, recover embedded `pimp_technique_*` / `pimp_shader_*` strings, and can emit a raw unknown-preserving Xenos dword listing. This is intentionally not yet a real opcode disassembler or shader IR translator; it preserves unknown words explicitly so the next decoder work has stable, diffable evidence.

`--write-disasm` writes the full raw listing to a deterministic artifact. When the output argument is a directory, the tool names the file from the microcode filename, for example:

- `shader_work/out/disasm/pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.xenos.asm`
- `shader_work/out/disasm/vertex_4cbe8078e63245600ee471c244caa9b01c3943e54859b5ce9cabeb96d961553b.xenos.asm`

`--write-ir` writes a raw unresolved backend-neutral JSON skeleton, for example:

- `shader_work/cache/ir/pixel_a4e37cc9c67eccf375d9329a62a8a92a6e545c4d8e792bfe02114ca3750219e1.bo2shaderir.json`

Current IR schema `bo2shaderir.raw_xenos.v1` contains stage, source path, source filename, byte/dword counts, embedded shader/technique names, empty input/output/constant/sampler/texture arrays, and one `unknown` instruction node per raw big-endian dword. It is useful cacheable structure for decoder work, but it is not yet semantic IR capable of generating HLSL or SPIR-V.

Ghidra evidence for the matching runtime path:

- XEX `0x82597DF8` loads a pass-relative shader record from `r5 + ((r10 + 0x70) << 3)`, reads microcode size at record offset `0x36c`, reads microcode offset at record offset `0x368`, adds pass base `*(r5 + 0x20)`, then emits/copies the microcode payload into the command buffer.
- XEX `0x82598140` calls the constant/argument uploader `0x82597F50` for PS-side record data at caller `0x82598310`, then reaches the `0x82597DF8` bind calls at `0x825985d8` and `0x825986a8`.
- PC/PDB `Material_LoadPassVertexShader` and `Material_LoadPassPixelShader` call `Material_Register*Shader`, then `Material_SetPassShaderArguments_DX`; `Material_SetPassShaderArguments_DX` uses D3D reflection to derive shader arguments and stream semantics. The XEX path therefore still needs recovered material argument metadata or an equivalent Xenos reflection/decode path before automatic HLSL generation can replace manual overrides.

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

## MP replay shader overrides added 2026-07-01

Two additional MP runtime shader pairs are now covered by manual D3D12
overrides while the complete Xenos-to-IR-to-HLSL translator is still incomplete:

- `VS=0xCBC9604F48930B36 / PS=0x7D1EF030F5710BDA`
- `VS=0x261BDD733FEC1F64 / PS=0xFF01D28E1EF3A880`
- `VS=0xCBC9604F48930B36 / PS=0x8645E8BA65E424B2`

Both vertex shaders decode to the same currently modeled replay interface:
`vf95` supplies `POSITION`, `COLOR0`, and `TEXCOORD0`; the Xenos shader exports
position, one UV interpolator, and one color interpolator. The D3D12 override
uses the already decoded screen-space replay vertices rather than the original
constant-driven matrix path.

`PS=0x7D1EF030F5710BDA` is only a conservative resource-backed approximation.
The semantic disassembly contains constant-driven address math and multiple
texture fetches:

```text
tfetch2D r1._x__, r1.xz, tf1
tfetch2D r1._x__, r2.xy, tf2
tfetch2D r1.__y_, r2.wy, tf2
tfetch2D r1.___z, r2.zy, tf2
mul o0.xyz0, r0.yzww, r0.xxxx
```

The override samples the captured textures but does not yet reproduce the full
ALU/constant behavior. It increases resource-backed D3D12 coverage but renders
very dark in the representative single-draw test.

`PS=0xFF01D28E1EF3A880` is a small glyph/UI shader and is a better manual
coverage win. Its semantic disassembly is:

```text
tfetch2D r1.1w__, r1.xy, tf1
mul o0, r1.xxxy, r0
```

The manual D3D12 override samples the captured texture with the interpolated UV
and modulates by vertex color. Single-draw replay of draw `566` renders visible
white glyph segments from real captured geometry/texture data, and full MP
replay increases real D3D12 submission to `110` draws across `4` shader pairs
with `diagnostic_pipelines=0`.

`PS=0x8645E8BA65E424B2` uses five captured texture fetches, which exposed a
real backend limit: the replay D3D12 root signature only exposed four SRVs and
four samplers. `D3D12ReplayBackend.cpp` now uses a layout8 descriptor table and
the shader cache binding-layout key is bumped from `layout4` to `layout8`.
This allows the five-texture shader to compile and create a PSO. Full MP replay
now submits `120` real D3D12 draws across `5` shader pairs with
`diagnostic_pipelines=0`. The manual `8645` override remains a conservative
texture-combine approximation and renders very dark until the real constant and
ALU behavior is lowered.

The current BO2-local translated-shader path is intentionally narrow. `native_shader_inspect.exe --compile-translated-hlsl-dxc` can lower and cache the `xenos_simple_passthrough_v1` subset seen in runtime VS `0xB6C9863F710683EC` and PS `0xA4A965C189287B99`, but it fails closed for the real-resource VS `0x5D918D91043B3ED0`. The next shader target is expanding semantic lowering for the real-resource shader pair currently covered by manual D3D12 overrides.

Latest real-resource semantic check on `shader_probe_capture_007`:

- VS `0x5D918D91043B3ED0`: one vertex binding (`vf95`), three attributes, `vfetch_full`, two `vfetch_mini` operations, `mad`, `sge`, seven `dp4` matrix/vector operations, `max oPos`, `max o0.xy__`, and `max o1`.
- PS `0xC4ED2979F29C9139`: four texture bindings (`tf4`, `tf3`, `tf2`, `tf1`), `mul`, `sge`, `sne`, `floors`, `mulsc`, `cndeq`, four `tfetch2D` operations, `dp2add`, `dp3`, `adds`, `add`, and final `mul o0`.

The VS side now has a limited generated lowering rule for this exact decoded pattern. It emits canonical replay-interface HLSL because replay has already decoded `vf95` into `POSITION`, `COLOR0`, and `TEXCOORD0`. This is a useful step beyond a manual VS override, but it is not a complete Xenos VS translator because the full constant-driven matrix path is not represented as general IR yet.

The PS side should not be represented by a small pattern rule. The next implementation target is a real semantic lowering path for pixel ALU/vector ops, texture fetch ops, swizzles/write masks, constants, and interpolator imports.

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

## Shader Record Probe Evidence

`shader_probe_capture_003` adds runtime shader/material record snapshots from the XEX `0x82597F50` ALU constant upload path. This hook captures argument registers, command-buffer write range, a primary record snapshot from `r4`, and a secondary snapshot from `r5` when it is pointer-like.

Verified probes:

| Function | LR | Primary | Secondary | Decoded name |
|---|---|---|---|---|
| `0x82597F50` | `0x82598314` | `0xA5BE7AE0` | `0xA6019BC0` | `pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb` |
| `0x82597F50` | `0x82598560` | `0xA5BE7374` | `0xA6019940` | `pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb` |

Probe layout observations:

- `primary_dwords[10]` is `0x52` for the PS record and `0x51` for the VS record. These values match the shader-name byte length, and the name bytes begin in big-endian order at `primary_dwords[11]`.
- The first record is a pixel-shader record (`ps_main_ps_3_0`) and its secondary pointer begins with nonzero floats followed by Xenos-looking shader words.
- The second record is a vertex-shader record (`vs_main_vs_3_0`) and its secondary pointer has leading zeros followed by Xenos-looking shader words.
- The 32-hex suffixes in these names, `534c8cc25dea1826410cc2974d7e9a80` and `e10bcefc8da60302d0bbf12b675d091c`, do not appear in `shader_work\shaders\index.json` or `shader_work\shaders\index.csv`.
- `native_shader_inspect.exe` now parses `shader_record_probe` rows from captures and reports stage guess, normalized shader name, name suffix, and raw/byte-swapped/trailing-zero-trimmed SHA-256 values of the secondary pointer payload.
- `native_shader_inspect.exe` also splits `pimp_shader_*` probe names into family, short hash, entry, and profile. The verified probe names decode as `family=cinematic short_hash=519f564 entry=main profile=ps_3_0` and `family=radiant short_hash=190f4788 entry=main profile=vs_3_0`.
- Probe-aware index matching on `shader_probe_capture_003` reports `names=0/2`, `suffixes=0/2`, `short_hashes=0/2`, and `secondary_payloads=0/2` against `shader_work\shaders\index.json`.

Conclusion: shader-record names are now observable at runtime and should become an additional registry key, but the extracted shader-work index still does not directly identify these names/suffixes. The next matching attempt should hash the secondary pointer payloads with the same raw, byte-swapped, trimmed, aligned, and container-stripped variants used for PM4 payloads, and then fall back to a manual override table keyed by `(stage, runtime_hash, shader_name)`.

## Safe Probe Follow-Up

Evidence date: 2026-06-30

The shader/material probe reader now guards guest dword loads on Windows and exposes `native_renderer_shader_record_probe_dwords` so probe snapshots can be reduced during crash triage without changing code. Default remains `32` dwords because 16-dword snapshots truncate `pimp_shader_*` names before the profile/suffix fields.

Verified capture:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode on --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --native_renderer_capture_limit 5000 --native_renderer_capture_flush_interval 32 --native_renderer_verbose false
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --validate
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --shader-record-probes --max-draws 16 --no-summary
native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --list-runtime-shaders --match-runtime-shaders --top-shaders 24
```

Results:

- Capture was watchdog-stopped after the bounded event limit and validates: `Validation OK: 5000 events, 24 frames, 1106 draws`.
- `native_shader_inspect.exe` reports `233` shader events, `8` unique runtime shaders, `7` shader pairs, `24` `shader_record_probe` events, and `233/233` shader payloads with payload.
- Recovered full probe names include:
  - PS `pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb`
  - VS `pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb`
  - PS `pimp_shader_trivial_63c48fc7_ps_main_ps_3_0_cd38a0f731a8c984a57c101f28952e56.updb`
  - VS `pimp_shader_trivial_63c48fc7_vs_main_vs_3_0_6c5717313f11297d9b331e326b80df12.updb`
- Direct runtime/static matching remains unproven: `Runtime shader direct matches: 0/8` and `Shader record probe matches: names=0/24 suffixes=0/24 short_hashes=0/24 secondary_payloads=0/24`.
- `native_shader_inspect.exe` now also compares the captured secondary shader-record dwords against runtime PM4 shader payload prefixes, constrained by the probe stage guess. On `shader_probe_capture_007`, this produces `12/24` probe prefix matches:
  - PS `pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb` -> runtime PS `0xC4ED2979F29C9139`, secondary offset `16` dwords, `16` matched payload dwords.
  - VS `pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb` -> runtime VS `0x5D918D91043B3ED0`, secondary offset `16` dwords, `16` matched payload dwords.
  - VS `pimp_shader_trivial_63c48fc7_vs_main_vs_3_0_6c5717313f11297d9b331e326b80df12.updb` -> runtime VS `0x81311AC4B1FBD082`, secondary offset `16` dwords, `16` matched payload dwords.
- The trivial PS probe does not match a runtime PM4 prefix in the first `32` captured secondary dwords. Its secondary block appears to start with constants or metadata, so the next rule needs either a larger safe secondary snapshot, the `0x82597DF8` shader-bind arguments, or a non-prefix/stripped payload search with false-positive guards.

Ghidra MCP follow-up on the XEX probe call sites:

- At `LR=0x82598314`, the caller passes `r4 = r29 + 0x28` and `r5 = *(r29 + 0x18)` into `0x82597F50`; the primary record contains the PS name and the secondary pointer contains constant/program-like data.
- At `LR=0x82598560`, the caller passes `r4 = r30 + 0x368` and `r5 = *(r30 + 0x20)` into `0x82597F50`; the primary record contains the VS name and the secondary pointer contains zero-padded shader/constant-like data.
- The same state block calls `0x82597DF8` after these uploads, which remains the stronger XEX runtime shader-bind target for correlating names to PM4 shader hashes.

## Manual Override Checkpoint

Draw `1209` in `shader_payload_capture_001` now has a documented D3D12 manual override pair:

- VS runtime hash `0x5D918D91043B3ED0`: `shader_work\native_overrides\d3d12\vs_5D918D91043B3ED0.hlsl`
- PS runtime hash `0xC4ED2979F29C9139`: `shader_work\native_overrides\d3d12\ps_C4ED2979F29C9139.hlsl`
- Manifest: `shader_work\native_overrides\overrides.json`

The backend accepts these files in strict `--backend d3d12` mode without `--allow-diagnostic-shader` and emits `DrawIndexedInstanced` using the real captured index and vertex buffers for draw `1209`. Replay now parses the manifest, compiles the HLSL pair to D3DCompile `.dxbc`, writes `shader_work\cache\shader_cache_index.json`, and can subsequently render from cache without the override source root. This is still a manual interface-compatible shader replacement, not decoded Xenos shader translation. The override uses the canonicalized position/color/UV stream, backend surface-size root constants, and a flattened block of captured BO2 constant dwords at `b1`; this proves constant payload binding but not a real Xenos constant-layout reconstruction.

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

## Runtime Semantic Xenos Disassembly

Evidence date: 2026-06-30

`native_shader_inspect.exe --semantic-disassemble` now links only the minimal ReXGlue shader analyzer sources into the BO2 project tool:

- `src/graphics/pipeline/shader/shader.cpp`
- `src/graphics/pipeline/shader/translator.cpp`
- `src/graphics/pipeline/shader/translator_disasm.cpp`
- `src/graphics/format/ucode.cpp`
- `src/graphics/xenos.cpp`

The tool does not link the full `rex::graphics` backend. A project-local `dump_shaders` CVar storage stub keeps `Shader::AnalyzeUcode` from pulling `graphics/flags.cpp`, RenderDoc, UI, D3D12 backend, or system runtime symbols into the standalone inspection tool.

Verified runtime commands:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --semantic-disassemble
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --semantic-disassemble
```

Results:

- Runtime VS `0xB6C9863F710683EC`: `payload_dwords=24`, `cf_pair_index_bound=3`, `register_static_address_bound=2`, writes interpolator `0`; ReXGlue disassembly decodes `exec`, `alloc interpolators`, `alloc position`, `max o0.0000, r0, r0`, and `max oPos.0001, r1, r1`.
- Runtime PS `0xA4A965C189287B99`: `payload_dwords=9`, `cf_pair_index_bound=1`, `register_static_address_bound=1`, writes interpolator `0`; ReXGlue disassembly decodes `alloc interpolators`, `exece`, and `max o0, r0, r0`.

This proves captured PM4 shader payloads can be semantically decoded by ReXGlue's Xenos analyzer and are now beyond raw word preservation for runtime shaders.

Runtime semantic artifact commands:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-semantic C:\Users\braxt\bo2-recompiled\shader_work\out\semantic --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-semantic C:\Users\braxt\bo2-recompiled\shader_work\out\semantic --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
```

Verified outputs:

- `shader_work\out\semantic\VS_0xB6C9863F710683EC.xenos.semantic.txt`
- `shader_work\cache\ir\VS_0xB6C9863F710683EC.semantic.bo2shaderir.json`
- `shader_work\out\semantic\PS_0xA4A965C189287B99.xenos.semantic.txt`
- `shader_work\cache\ir\PS_0xA4A965C189287B99.semantic.bo2shaderir.json`

The runtime semantic IR schema is `bo2shaderir.semantic_xenos.v1`. It currently records source capture, runtime hash, draw/load counts, payload hashes, control-flow bound, register bound, export metadata, decoded disassembly lines, and raw backing words. It is a semantic inspection/cache artifact, not yet a complete executable shader IR for HLSL generation.

Diagnostic runtime HLSL scaffolding is now available for captured runtime shader hashes:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
```

Verified outputs:

- `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.diagnostic.hlsl`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.diagnostic.hlsl`

These files are deliberately labeled diagnostic. They define stable entry points and placeholder bindings for captured constants at `b1`, texture `t0`, and sampler `s0`, but they do not lower decoded Xenos ALU/texture operations yet.

The diagnostic HLSL can also be compiled into a D3D12 cache artifact:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache
```

Verified outputs:

- `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.diagnostic.dxbc`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.diagnostic.dxbc`
- `shader_work\cache\logs\VS_0xB6C9863F710683EC.diagnostic.log`
- `shader_work\cache\logs\PS_0xA4A965C189287B99.diagnostic.log`
- `shader_work\cache\diagnostic_shader_cache_index.jsonl`

This path uses `D3DCompile` because that is already used by the current replay backend. It is a compiler/cache proof for generated runtime shader artifacts, not DXC/DXIL and not real Xenos operation lowering.

Diagnostic DXC/DXIL compilation is now also available:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
```

Verified outputs:

- `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.diagnostic.dxc.dxil`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.diagnostic.dxc.dxil`
- `shader_work\cache\logs\VS_0xB6C9863F710683EC.diagnostic.dxc.dxc.log`
- `shader_work\cache\logs\PS_0xA4A965C189287B99.diagnostic.dxc.dxc.log`
- `shader_work\cache\shader_cache_index.jsonl`

The current host auto-discovers DXC at `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe`. A repeated VS compile reports `cache_hit=true`. This proves DXC/DXIL cache plumbing for generated diagnostic shaders, not real Xenos operation lowering.

Limited translated HLSL is now available for the simplest decoded runtime shader subset:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-translated-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-translated-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
```

Verified outputs:

- `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.translated.hlsl`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.translated.hlsl`
- `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.translated.dxil`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.translated.dxil`

The supported subset is `xenos_simple_passthrough_v1`:

- VS `0xB6C9863F710683EC`: lowers `max o0.0000, r0, r0` and `max oPos.0001, r1, r1` into a no-input `SV_VertexID` vertexless point-draw variant for the captured no-fetch point-list class. This is deliberately scoped to `B6C986/A4A965` non-indexed primitive-type-`1` draws; other no-fetch classes remain unsupported until their shader/export behavior is decoded.
- PS `0xA4A965C189287B99`: lowers `max oC0, r0, r0` into `return max(input.r0, input.r0)`.

Unsupported shaders fail closed. For example, VS `0x5D918D91043B3ED0` currently exits `1` with `no limited translated-HLSL rule`.

The runtime semantic IR now serializes analyzer-exposed shader interface metadata in addition to text disassembly:

- constant float/bool/loop/vertex-fetch bitmaps and dynamic addressing flags
- vertex fetch bindings, fetch constants, stride, attribute write masks, Xenos formats, offsets, and signed/integer flags
- texture fetch bindings, fetch constants, dimension, result write masks, filter override fields, and computed-LOD flags
- structured operation records split from ReXGlue semantic disassembly into `address`, `opcode`, `operands`, and original `text`

Verified on the current real-resource manual override pair:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0x5D918D91043B3ED0 --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xC4ED2979F29C9139 --write-semantic-ir C:\Users\braxt\bo2-recompiled\shader_work\cache\ir
```

Observed metadata:

- VS `0x5D918D91043B3ED0`: `float_count=10`, `vertex_fetch_bitmap[2]=0x80000000`, one vertex binding on fetch constant `95`, stride `8` dwords, with attributes for Xenos formats `38`, `6`, and `37`.
- PS `0xC4ED2979F29C9139`: `float_count=5`, four `tfetch2D` texture bindings using fetch constants `4`, `3`, `2`, and `1`.
- The generated `operations` arrays include real opcode records for `vfetch_full`, `vfetch_mini`, `mad`, `dp4`, `tfetch2D`, `mul`, and `dp2add`.
- Operation records now include normalized `category`, `coissued`, `dest`, `sources`, register file/index/mask metadata, and `fetch_constant` for texture/vertex fetches. On real-resource PS `0xC4ED2979F29C9139`, semantic IR contains `31` typed operations, `4` texture-fetch operations, `5` co-issued scalar operations, and one typed `oC0` color export.
- Representative typed PS records now cover `tfetch2D r0.__x_, r0.xy, tf4` with `fetch_constant=4`, `cndeq r2.xy__, r0.wwww, r0.xyyy, r2.xyyy`, co-issued `floors r0.___w, r0.w`, `dp2add r0.x___, r3.zxxx, c255.wyyy, c252.yyyy`, and final `mul oC0, r0.xywz, r1`.
- The semantic IR emitter and limited HLSL translator now share the same parsed operation records. The real-resource PS rule checks decoded opcode, destination operand, and fetch-constant metadata instead of raw disassembly substrings, which makes the next shader rules less brittle.
- `xenos_limited_semantic_v5` now uses that typed real-resource PS evidence to generate cacheable HLSL for PS `0xC4ED2979F29C9139`. This is not a complete instruction-by-instruction PS translator yet: it recognizes the four-`tfetch2D`/export pattern and maps `tf4/tf3/tf2/tf1` to the four per-draw D3D12 replay descriptors `t0/t1/t2/t3`.
- The same v5 translator adds non-indexed VS `0xAB1E86137A0240E8`, matching `vfetch_full r0.xy11` and `max oPos, r0, r0`. D3D12 replay on explicit draw `25` of `sidecar_capture_002` now succeeds with generated cache records and writes `native-renderer-ab1e-nonindexed-d3d12-real.bmp`, SHA-256 `A20B0105C2961DEB0BD9AEFBD91CD34E69D483FACC52AB2AD037099DBE7B7EB8`.
- `xenos_limited_semantic_v6` adds a hash-keyed rule for runtime VS `0x81311AC4B1FBD082`, whose semantic decode includes `vfetch_full r1.xyz_` and `vfetch_mini r0.xy__` from vf95, `dp4 oPos`, and `mul o0`. The generated HLSL preserves the captured resource geometry through the replay canonical `POSITION`/`COLOR0`/`TEXCOORD0` interface while full constant-driven matrix lowering is still incomplete.
- v6 also adds a hash-keyed partial rule for runtime PS `0x246E20EF10E0DDC7`, which has six `tfetch2D` operations through fetch constant `0`, predicated/scalar ALU, and a final `mad oC0.xyz1`. The generated HLSL samples the first four replay texture descriptors and uses captured constants only as bias terms. This is intentionally a resource-binding and post-process bring-up path, not a complete lowering of the shader's predicated Xenos ALU.
- Verified v6 outputs on `sidecar_capture_002`: `VS_0x81311AC4B1FBD082.translated.v6.dxc.dxil` and `PS_0x246E20EF10E0DDC7.translated.v6.dxc.dxil` compile with DXC. Explicit D3D12 replay of draw `1013` submits one real indexed draw, binds `4` captured SRVs/samplers, applies draw `1013` render state, and writes `native-renderer-246e-draw1013-v6-d3d12-real.bmp` with SHA-256 `BA999F177A69BAA48BFB044B9B0142A05EF7EE6E013570F47EEF087FC04B0A70`. The image is only a partial band because the captured format-2 texture sidecars are truncated previews and the PS ALU lowering is incomplete.
- D3D12 replay on `sidecar_capture_002` still succeeds with an empty override root and generated cache records for both `VS=0x5D918D91043B3ED0` and `PS=0xC4ED2979F29C9139`, submitting `5/5` supported indexed draws, binding `20` captured texture SRVs plus `20` captured samplers, and writing `native-renderer-v5-multitexture-regression-d3d12-real.bmp` with SHA-256 `AF172ED5685D34C32696219E3C9E8630CDB8085341613BEFDBA2E3F85EF86FEB`.
- The D3D12 replay cache resolver now selects PS variants by VS/PS pair for shared runtime PS hashes. `AB1E86137A0240E8` exports through `TEXCOORD0` without `COLOR0`, so its `C4ED2979F29C9139` and `246E20EF10E0DDC7` pairings use UV-only v7 PS variants. Indexed `5D918D/C4ED` and `81311A/246E` keep the earlier color+UV PS variants. On `vertex_recapture_001`, direct draws `698` and `910` now create PSOs and submit, and frame skip replay submits all `260/260` geometry-supported draws across `7` shader pairs.
- The D3D12 replay resolver now also has a vertexless v8 VS variant for the runtime top pair `B6C986/A4A965`. On `vertex_recapture_001`, direct draw `0` submits `2090/2090` supported draws for that pair, and frame skip replay submits `2350` supported draws across `8` shader pairs. Strict frame replay now elides three no-side-effect `DDED7E/A4A965` draws and fails at the enabled-color `DDED7E/3A6876` point-draw class, proving the B6/A4 path did not become a broad no-fetch fallback.

Static extracted `.ucode` files remain unresolved for semantic decode. They still dump raw words and raw IR correctly, but the tested static files begin with extracted metadata/constants rather than the exact runtime shader payload layout expected by `Shader::AnalyzeUcode`. A first auto-offset scan was removed because a wrong static offset can make the analyzer run too long. The next static-file task is to reverse the extracted `.ucode` layout or use the container descriptor fields to pass only the true microcode program range to the analyzer.

## Native renderer path forward

1. Preserve the runtime `(stage, hash, guest_address, dword_count)` stream from replay as the first shader registry key.
2. Extend capture with material/pass identity, sampler bindings, texture fetch constants, and full constant payload bytes.
3. Map runtime hashes back to `shader_work` container/microcode records where possible.
4. Add a manual replacement table keyed by stage plus runtime hash.
5. Let the null/debug and replay backends report missing shader mappings before a real backend attempts to draw.
6. Prefer a backend-neutral shader IR or metadata layer so D3D12, Vulkan, Metal, and deko3d can share the same mapping database.
