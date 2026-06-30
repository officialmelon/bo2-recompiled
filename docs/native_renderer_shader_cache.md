# Native Renderer Shader Cache

Last updated: 2026-06-29

## Implemented

`native_shader_inspect.exe` has been added as the first shader registry inspection tool.

A first manual D3D12 override pair exists for the draw `1209` runtime shader hashes:

- Manifest: `shader_work\native_overrides\overrides.json`
- Vertex override: `shader_work\native_overrides\d3d12\vs_5D918D91043B3ED0.hlsl`
- Pixel override: `shader_work\native_overrides\d3d12\ps_C4ED2979F29C9139.hlsl`

The D3D12 replay backend now resolves shaders in this order:

1. `shader_work\cache\shader_cache_index.json` runtime-hash cache hit.
2. Parsed `shader_work\native_overrides\overrides.json`.
3. Deterministic runtime-hash filenames under `shader_work\native_overrides\d3d12`.
4. Explicit diagnostic shader only when `--allow-diagnostic-shader` is supplied.
5. Fail closed.

The compiled cache currently stores D3DCompile output (`.dxbc`) because `dxc.exe` is not available on the current PATH. DXC/DXIL is still required for the final shader pipeline.

Verified cache artifacts for draw `1209` with the original constant-only binding layout:

- `shader_work\cache\shader_cache_index.json`
- `shader_work\cache\d3d12\manual_vs_5D918D91043B3ED0_vs_5_0_layout2_src7E9F55F7C18FD853.dxbc`
- `shader_work\cache\d3d12\manual_ps_C4ED2979F29C9139_ps_5_0_layout2_src43AD01F616D81230.dxbc`
- `shader_work\cache\logs\manual_vs_5D918D91043B3ED0_vs_5_0_layout2_src7E9F55F7C18FD853.log`
- `shader_work\cache\logs\manual_ps_C4ED2979F29C9139_ps_5_0_layout2_src43AD01F616D81230.log`

The current D3D12 replay binding layout is `layout4`, which adds a captured texture SRV descriptor table at `t0` and a dynamic sampler descriptor table at `s0`. Verified current cache artifacts:

- `shader_work\cache\shader_cache_index.json`
- `shader_work\cache\d3d12\manual_vs_5D918D91043B3ED0_vs_5_0_layout4_src7E9F55F7C18FD853.dxbc`
- `shader_work\cache\d3d12\manual_ps_C4ED2979F29C9139_ps_5_0_layout4_src629DEA58066BB4CE.dxbc`
- `shader_work\cache\logs\manual_vs_5D918D91043B3ED0_vs_5_0_layout4_src7E9F55F7C18FD853.log`
- `shader_work\cache\logs\manual_ps_C4ED2979F29C9139_ps_5_0_layout4_src629DEA58066BB4CE.log`

Verified behavior:

- Empty cache root plus default override root compiles from `overrides.json`, writes `.dxbc` blobs/logs/index, and renders draw `1209`.
- Default cache root plus empty override root renders draw `1209` from cache only.
- Empty cache root plus empty override root fails closed with the missing VS/PS runtime hashes.
- `state_capture_004` real D3D12 replay compiles the layout4 override pair and reports `D3D12 real replay bound 4 captured texture SRV(s), 0 fallback texture SRV(s), unsupported_texture_attempts=0` and `D3D12 real replay bound 4 captured sampler descriptor(s), 0 fallback sampler descriptor(s)`.
- Cache-only layout4 replay with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache` also succeeds on `state_capture_004`, binds the same `4` captured texture SRVs and `4` captured sampler descriptors, and writes `native-renderer-state-capture-004-d3d12-sampler-bound-cache-only.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.

Current supported commands:

```powershell
native_shader_inspect.exe --index shader_work\shaders\index.json --summary
native_shader_inspect.exe --index shader_work\shaders\index.json --hash <hash-or-substring> --find
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\vertex_fetch_capture_001\events.jsonl --list-runtime-shaders --top-shaders 20
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\vertex_fetch_capture_001\events.jsonl --match-runtime-shaders --top-shaders 20
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\shader_payload_capture_001\events.jsonl --list-runtime-shaders --match-runtime-shaders --top-shaders 20
native_shader_inspect.exe --microcode shader_work\shaders\microcode\<stage_hash>.ucode --write-disasm shader_work\out\disasm
native_shader_inspect.exe --microcode shader_work\shaders\microcode\<stage_hash>.ucode --write-ir shader_work\cache\ir
native_shader_inspect.exe --capture native_captures\shader_probe_capture_007\events.jsonl --hash <runtime_shader_hash> --semantic-disassemble
```

Verified summary:

- Zones: `142`
- Occurrences: `36769`
- Unique containers: `33950`
- Pixel containers: `31134`
- Vertex containers: `2816`
- Unique microcode programs: `33355`
- Invalid bounds: `0`

Verified runtime matching on `vertex_fetch_capture_001`:

- Lines: `12000`
- Shader events: `594`
- Draw events: `2646`
- Unique runtime shaders: `8`
- Runtime shader pairs: `7`
- Direct runtime-hash substring matches in static index: `0/8`
- Runtime draw `1004` pair `VS=0x5D918D91043B3ED0`, `PS=0xC4ED2979F29C9139` remains unmatched against static container/microcode hashes.

Verified runtime matching on `shader_payload_capture_001`:

- Lines: `12000`
- Shader events: `587`
- Draw events: `2654`
- Unique runtime shaders: `8`
- Runtime shader pairs: `7`
- Shader payload coverage: `587/587` uploads, `13230` payload dwords, `0` missing, `0` truncated
- Direct runtime-hash substring matches in static index: `0/8`
- Draw `1209` pair `VS=0x5D918D91043B3ED0`, `PS=0xC4ED2979F29C9139`
- `native_shader_inspect.exe` reports stable captured payload hashes for all `8` runtime shaders: `payload_hash_mismatches=0` for every listed shader.
- Direct static-index matches remain `0/8` for runtime IDs, raw little-endian payload SHA-256, raw big-endian payload SHA-256, trailing-zero-trimmed little-endian payload SHA-256, and trailing-zero-trimmed big-endian payload SHA-256.

Top/target payload hash examples from `shader_payload_capture_001`:

| Stage | Runtime hash | Raw LE SHA-256 | Raw BE SHA-256 | Trimmed LE SHA-256 | Trimmed BE SHA-256 |
|---|---|---|---|---|---|
| PS | `0xA4A965C189287B99` | `f73f655ea80c22bde4bc93575f87664e81056b6f4714bef3a4defaa18994d18a` | `9d4a64e81064abb794c5cba3642feda86a5a1e6b75a13cd4c4d7917bea83e01e` | `d9a941788ec3e15d7c99028a431d430a6c0d20cd3e30cb4b0ee275150ce94c7a` | `c8a77c02cb3e2c3af1a3749409b7b004cd638e37a9370d708467ea92e25f6ee1` |
| VS | `0xB6C9863F710683EC` | `7d63b0bdd7ccfaec713a55d66a8491b0055ad48f70c7a9d8703254e6f0a70d5e` | `a5ffccb2a2cdccd0093b55f9051330166881a987052e76249b4841ee7170c608` | `c3c4c36300a6debba80def0b6e94d7f26fa318b569ec9a636e5235cc5238bb21` | `57f0bea0ec74d3485a67347ddbeb3a2c185db33187d6cd7fc4c9d72ade91e602` |
| PS | `0xC4ED2979F29C9139` | `330eec0ea9acf5248701e56987cd934a1440721f1120e7308537af69c14a7baf` | `6b4da9f6a56a33138bd7a808177fef38133c5c9dc89b257682250829a5ff1281` | `330eec0ea9acf5248701e56987cd934a1440721f1120e7308537af69c14a7baf` | `6b4da9f6a56a33138bd7a808177fef38133c5c9dc89b257682250829a5ff1281` |
| VS | `0x5D918D91043B3ED0` | `44b6f5e82fa042067532265cecd6a884c37521c5a544dfa2b1c9ac56702be88f` | `776490d2bb3e6c3d2f27d8443f7c930d2ea326f9d429ee5a1b8b9255aa1c39e4` | `44b6f5e82fa042067532265cecd6a884c37521c5a544dfa2b1c9ac56702be88f` | `776490d2bb3e6c3d2f27d8443f7c930d2ea326f9d429ee5a1b8b9255aa1c39e4` |

## Runtime semantic analyzer checkpoint

`native_shader_inspect.exe` now has a runtime semantic disassembly path backed by ReXGlue's Xenos analyzer:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --semantic-disassemble
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --semantic-disassemble
```

Verified output:

- VS `0xB6C9863F710683EC`: `cf_pair_index_bound=3`, `register_static_address_bound=2`, D3D-style Xenos disassembly containing `exec`, `alloc interpolators`, `alloc position`, and `max` exports.
- PS `0xA4A965C189287B99`: `cf_pair_index_bound=1`, `register_static_address_bound=1`, D3D-style Xenos disassembly containing `alloc interpolators`, `exece`, and `max o0, r0, r0`.
- Runtime semantic text and IR artifacts can now be written directly from capture/hash:
  - `shader_work\out\semantic\VS_0xB6C9863F710683EC.xenos.semantic.txt`
  - `shader_work\cache\ir\VS_0xB6C9863F710683EC.semantic.bo2shaderir.json`
  - `shader_work\out\semantic\PS_0xA4A965C189287B99.xenos.semantic.txt`
  - `shader_work\cache\ir\PS_0xA4A965C189287B99.semantic.bo2shaderir.json`

This is not yet cached translated shader output. It is the first real semantic decode layer for captured runtime payloads and is the input for the next semantic IR/HLSL pass.

Static extracted `.ucode` semantic decode is still blocked by file-layout ambiguity. Raw `.ucode` artifact commands still work, but tested static files include metadata/constants before the analyzer's expected payload range.

## Not implemented yet

- Proven runtime 64-bit shader hash to static container/microcode matching.
- Complete static-file Xenos shader disassembler.
- Complete executable backend-neutral shader IR for runtime shaders. A raw unresolved `bo2shaderir.raw_xenos.v1` JSON skeleton exists for static `.ucode`, and runtime `bo2shaderir.semantic_xenos.v1` inspection artifacts now exist, but they are not yet a complete HLSL-ready operation graph.
- HLSL/SPIR-V generation.
- DXC integration.
- Persistent compiled shader cache.
- DXC/DXIL compiler integration.
- Cache entries for automatically translated Xenos shaders.

## Current matching rule

Runtime replay hashes are treated as distinct runtime IDs. They are not assumed to equal static SHA-256 container or microcode hashes. The current direct substring rule finds `0/8` runtime shader hashes in `shader_work/shaders/index.json`, and the reproducible PM4 payload rules also find `0/8` across raw LE, raw BE, trailing-zero-trimmed LE, and trailing-zero-trimmed BE SHA-256 variants. Matching must now use aligned/padded payload variants, shader record metadata, container headers, Ghidra-backed material records, or manual mapping evidence.
