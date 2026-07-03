# Native Renderer Shader Cache

Last updated: 2026-07-03

## Implemented

`native_shader_inspect.exe` has been added as the first shader registry inspection tool.

A first manual D3D12 override pair exists for the draw `1209` runtime shader hashes:

- Manifest: `shader_work\native_overrides\overrides.json`
- Vertex override: `shader_work\native_overrides\d3d12\vs_5D918D91043B3ED0.hlsl`
- Pixel override: `shader_work\native_overrides\d3d12\ps_C4ED2979F29C9139.hlsl`

The D3D12 replay backend now resolves shaders in this order:

1. Non-manual `shader_work\cache\shader_cache_index.json` runtime-hash cache hit.
2. Non-manual `shader_work\cache\shader_cache_index.jsonl` runtime-hash cache hit.
3. Non-manual deterministic translated cache filenames under `shader_work\cache\d3d12`.
4. Parsed `shader_work\native_overrides\overrides.json`.
5. Deterministic runtime-hash filenames under `shader_work\native_overrides\d3d12`.
6. Explicit diagnostic shader only when `--allow-diagnostic-shader` is supplied.
7. Fail closed.

Real replay ignores cache entries marked `"diagnostic": true`; JSONL cache entries may point at either `.dxbc` or `.dxil` blobs.
JSONL cache entries may also include a `source` HLSL path and `entry`; if the cache blob is missing, the D3D12 replay backend can compile that source into the requested cache path.
Real replay now deliberately skips cache records whose cache key starts with `manual_` when it is looking for automatic translated cache hits. Manual shaders remain available only through the explicit override fallback path.

The manual D3D12 replay override cache still stores D3DCompile output (`.dxbc`). A separate generated diagnostic DXC path now stores `.dxil`; real translated-shader DXC/DXIL is still required for the final shader pipeline.

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
- JSONL cache-only replay was verified with a temporary cache root containing only `shader_cache_index.jsonl` and no override manifest. On `sidecar_capture_002`, `native_render_replay.exe --backend d3d12 --shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache-jsonl-replay-test` submitted `5/5` supported draws, bound `5` captured texture SRVs and `5` captured samplers, applied captured render state from draw `748`, and wrote `native-renderer-cache-jsonl-d3d12-real.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Mixed generated/manual shader replay was verified with `shader_work\cache-mixed-translated-vs-source-test-002\shader_cache_index.jsonl`: VS `0x5D918D91043B3ED0` came from generated `xenos_limited_semantic_v2` HLSL and compiled through replay-side `D3DCompile` to `shader_work\cache-mixed-translated-vs-source-test-002\d3d12\VS_0x5D918D91043B3ED0.translated.v2.d3dcompile.dxbc` (`17148` bytes), while PS `0xC4ED2979F29C9139` came from the existing manual cached PS blob. With `--shader-override-root native_captures\empty_shader_overrides`, replay submitted `5/5` supported draws and wrote `native-renderer-mixed-translated-vs-source-002-d3d12-real.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.

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
native_shader_inspect.exe --capture native_captures\shader_probe_capture_007\events.jsonl --hash <runtime_shader_hash> --write-hlsl shader_work\cache\hlsl
native_shader_inspect.exe --capture native_captures\shader_probe_capture_007\events.jsonl --hash <runtime_shader_hash> --compile-hlsl shader_work\cache
native_shader_inspect.exe --capture native_captures\shader_probe_capture_007\events.jsonl --hash <runtime_shader_hash> --compile-hlsl-dxc shader_work\cache
native_shader_inspect.exe --capture native_captures\shader_probe_capture_007\events.jsonl --hash <runtime_shader_hash> --write-translated-hlsl shader_work\cache\hlsl
native_shader_inspect.exe --capture native_captures\shader_probe_capture_007\events.jsonl --hash <runtime_shader_hash> --compile-translated-hlsl-dxc shader_work\cache
native_shader_inspect.exe --capture native_captures\live_d3d12_mp_080_shader_probe_write_snapshot\events.jsonl --precompile-runtime-shaders-d3d12 shader_work\cache --top-shaders 50
```

## Runtime D3D12 Precompile Checkpoint

Evidence date: 2026-07-03

`native_shader_inspect.exe --precompile-runtime-shaders-d3d12 <cache_root>` now precompiles the top runtime-used captured shaders into the persistent D3D12 shader cache. The command ranks captured shader pairs by draw count, skips draw-unused shaders, skips incomplete PM4 payloads, and reports each shader as `compiled`, `cache_hit`, `translator_failed`, `dxc_failed`, or `no_payload`.

Validation command:

```powershell
C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native_shader_inspect.exe --capture native_captures\live_d3d12_mp_080_shader_probe_write_snapshot\events.jsonl --precompile-runtime-shaders-d3d12 shader_work\cache --top-shaders 50
```

Verified result:

- `attempted=16`
- `compiled=1`
- `cache_hits=7`
- `no_payload=0`
- `translator_failed=9`
- `dxc_failed=0`
- cache index: `shader_work\cache\shader_cache_index.jsonl`

Important successful runtime shaders:

| Stage | Runtime hash | Result |
|---|---|---|
| PS | `0xA4A965C189287B99` | cache hit |
| VS | `0xB6C9863F710683EC` | cache hit |
| PS | `0xEDC17DCC3FFDB040` | cache hit |
| VS | `0x3C4F6D40D699817B` | cache hit |
| VS | `0x1E6883FCCDE1F688` | cache hit |
| VS | `0xAB1E86137A0240E8` | cache hit |
| PS | `0x3A6876055FEC1674` | cache hit |
| VS | `0x5B9B7484417FB9B6` | compiled |

Top remaining translator failures from this capture:

| Stage | Runtime hash | Draws | Current blocker |
|---|---:|---:|---|
| PS | `0xFF01D28E1EF3A880` | 197 | no limited translated-HLSL rule for `tfetch2D`, `mul oC0`, and alpha/color path |
| VS | `0xCBC9604F48930B36` | 169 | no rule for multi-fetch vertex shader with `vfetch_full` plus multiple `vfetch_mini` ops |
| VS | `0x261BDD733FEC1F64` | 154 | same multi-fetch vertex shader class as above |
| PS | `0x7D1EF030F5710BDA` | 88 | no rule for `mul`, `addsc`, `frc`, `frcs`, `cndge` ALU path |
| PS | `0x8645E8BA65E424B2` | 44 | no rule for larger multi-texture/ALU shader |
| PS | `0x79E1F538A5074A65` | 37 | captured PM4 payload is truncated at `512` dwords; needs uncapped payload capture before translation |
| VS | `0xDDED7E538422AE73` | 25 | no-raster/no-fetch utility shader; currently classified outside scene rendering |
| VS | `0xEF95534343684F5B` | 9 | no limited translated-HLSL rule yet |
| PS | `0xDC168FB6031AFC41` | 9 | no limited translated-HLSL rule yet |

The truncated-payload skip is intentional: running ReXGlue's analyzer on incomplete shader PM4 payloads can assert. The correct fix for `PS 0x79E1F538A5074A65` is capture completeness, not a guessed shader replacement.

Capture-side follow-up:

- `PM4ShaderInfo::kMaxPayloadDwords` was raised from `512` to `4096` dwords.
- The known truncated MP080 shader reports `max_dwords=1293`, so the new cap
  should allow future captures to contain its full PM4 shader payload.
- Constant payload capture remains capped separately at `64` dwords; this
  change is shader-specific.

Fresh capture validation:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode on --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_cap4096_001\events.jsonl --native_renderer_capture_limit 18000 --native_renderer_capture_flush_interval 32 --native_renderer_verbose false
native_render_replay.exe --capture native_captures\shader_payload_cap4096_001\events.jsonl --validate --no-summary
native_shader_inspect.exe --capture native_captures\shader_payload_cap4096_001\events.jsonl --precompile-runtime-shaders-d3d12 shader_work\cache --top-shaders 20
```

Results:

- Capture run was watchdog-stopped after `120` seconds and wrote
  `native_captures\shader_payload_cap4096_001\events.jsonl`.
- Replay validation passed:
  `Validation OK: 18000 events, 45 frames, 2866 draws`.
- Shader payload coverage improved to
  `with_payload=1438 missing_payload=0 payload_dwords=65433 truncated=0`.
- Runtime D3D12 precompile reported `attempted=11`, `compiled=3`,
  `cache_hits=7`, `translator_failed=1`, `dxc_failed=0`.
- Newly compiled translated DXIL shaders:
  `VS 0x81311AC4B1FBD082`, `PS 0xC4ED2979F29C9139`,
  and `VS 0x5D918D91043B3ED0`.
- Remaining translator miss in this capture:
  `VS 0xDDED7E538422AE73`, a no-raster/no-fetch utility-style shader with
  first ops `[alloc interpolators; alloc position; exece; setp_clr; sqrt oPos; cnop]`.

Shared-translator validation:

The high-traffic limited translator rules for `VS 0xB6C9863F710683EC`,
`VS 0x5B9B7484417FB9B6`, `VS 0x1E6883FCCDE1F688`,
`VS 0xAB1E86137A0240E8`, `VS 0x81311AC4B1FBD082`,
`VS 0x5D918D91043B3ED0`, `PS 0x246E20EF10E0DDC7`,
`PS 0xC4ED2979F29C9139`, `PS 0x3A6876055FEC1674`, and the generic
`max oC0` pixel export class now live in
`src\native_renderer\shader_translation\XenosHlslTranslator.cpp` instead of
only in the `native_shader_inspect` CLI fallback.

An isolated cache-root run proves those shared rules can generate and compile
runtime shaders without relying on pre-existing cache artifacts:

```powershell
native_shader_inspect.exe --capture native_captures\shader_payload_cap4096_001\events.jsonl --precompile-runtime-shaders-d3d12 native_captures\tmp_shared_translator_cache --top-shaders 20
```

Result:

- `attempted=11`
- `compiled=10`
- `cache_hits=0`
- `translator_failed=1`
- `dxc_failed=0`
- Generated HLSL provenance: `files=10`, `translation_source: shared=10`,
  `native_shader_inspect_fallback=0`.
- The precompile command now reports provenance directly; the same run prints
  `source_shared=10 source_fallback=0 source_unknown=0`.

The only shared-translator miss remains `VS 0xDDED7E538422AE73`, which is still
handled as no-raster/no-fetch utility work by the D3D12 gap classifier rather
than as visible scene geometry.

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

## Diagnostic HLSL scaffold

`native_shader_inspect.exe --write-hlsl` now writes metadata-driven diagnostic HLSL from a captured runtime shader hash. Verified commands:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
```

Outputs:

- `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.diagnostic.hlsl`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.diagnostic.hlsl`

These files are explicitly marked diagnostic in comments and are not real Xenos translations. They provide the first generator/cache layout for runtime shader metadata, constants at `b1`, texture/sampler placeholders at `t0/s0`, and a stable entry point shape for later DXC/DXIL wiring.

`native_shader_inspect.exe --compile-hlsl` now compiles that diagnostic HLSL through the same Windows `D3DCompile` dependency family used by the D3D12 replay backend. Verified commands:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache
```

Outputs:

- `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.diagnostic.hlsl`
- `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.diagnostic.dxbc`
- `shader_work\cache\logs\VS_0xB6C9863F710683EC.diagnostic.log`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.diagnostic.hlsl`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.diagnostic.dxbc`
- `shader_work\cache\logs\PS_0xA4A965C189287B99.diagnostic.log`
- `shader_work\cache\diagnostic_shader_cache_index.jsonl`

This proves a generated runtime-shader artifact can enter a persistent D3D12 cache path, but it is still diagnostic DXBC, not real translated Xenos shader output. The DXC/DXIL diagnostic path below is separate from this D3DCompile path.

## Diagnostic DXC/DXIL cache

`native_shader_inspect.exe --compile-hlsl-dxc` now compiles generated diagnostic HLSL through DXC and writes DXIL cache entries. The tool auto-discovers the installed Windows SDK DXC at:

```text
C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe
```

Verified commands:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
```

The first two commands compile VS/PS DXIL and print `cache_hit=false`. The repeated VS command prints `cache_hit=true`.

Outputs:

- `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.diagnostic.dxc.hlsl`
- `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.diagnostic.dxc.dxil`
- `shader_work\cache\logs\VS_0xB6C9863F710683EC.diagnostic.dxc.dxc.log`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.diagnostic.dxc.hlsl`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.diagnostic.dxc.dxil`
- `shader_work\cache\logs\PS_0xA4A965C189287B99.diagnostic.dxc.dxc.log`
- `shader_work\cache\shader_cache_index.jsonl`

This satisfies a diagnostic DXC/DXIL cache milestone only. It still does not translate decoded Xenos operations into real BO2 shader HLSL.

## Limited Translated HLSL

`native_shader_inspect.exe --write-translated-hlsl` now writes a separate translated HLSL artifact for a narrow decoded Xenos operation subset. Verified commands:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --write-translated-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --write-translated-hlsl C:\Users\braxt\bo2-recompiled\shader_work\cache\hlsl
```

Outputs:

- `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.translated.hlsl`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.translated.hlsl`

These files are not diagnostic fallbacks. They are generated only when the decoded operations match `xenos_simple_passthrough_v1`. The generated VS/PS compile with DXC to:

- `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.translated.dxil`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.translated.dxil`

`native_shader_inspect.exe --compile-translated-hlsl-dxc` now performs that DXC compile directly and writes persistent non-diagnostic cache records:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-translated-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xA4A965C189287B99 --compile-translated-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl --hash 0xB6C9863F710683EC --compile-translated-hlsl-dxc C:\Users\braxt\bo2-recompiled\shader_work\cache
```

Verified outputs:

- `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.translated.dxc.hlsl`
- `shader_work\cache\d3d12\VS_0xB6C9863F710683EC.translated.dxc.dxil`
- `shader_work\cache\logs\VS_0xB6C9863F710683EC.translated.dxc.dxc.log`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.translated.dxc.hlsl`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.translated.dxc.dxil`
- `shader_work\cache\logs\PS_0xA4A965C189287B99.translated.dxc.dxc.log`
- `shader_work\cache\shader_cache_index.jsonl`

The first VS/PS compile prints `cache_hit=false`; a repeated VS command prints `cache_hit=true`. The real-resource VS `0x5D918D91043B3ED0` now generates HLSL from the decoded `vfetch`/`dp4`/export pattern and compiles to:

- `shader_work\cache\hlsl\VS_0x5D918D91043B3ED0.translated.hlsl`
- `shader_work\cache\hlsl\VS_0x5D918D91043B3ED0.translated.v2.dxc.hlsl`
- `shader_work\cache\d3d12\VS_0x5D918D91043B3ED0.translated.v2.dxc.dxil`
- `shader_work\cache\logs\VS_0x5D918D91043B3ED0.translated.v2.dxc.dxc.log`

The `xenos_limited_semantic_v5` cache version keeps the generated real-resource shader pair and adds generated HLSL for non-indexed VS `0xAB1E86137A0240E8`:

- `shader_work\cache\hlsl\VS_0xAB1E86137A0240E8.translated.v5.dxc.hlsl`
- `shader_work\cache\d3d12\VS_0xAB1E86137A0240E8.translated.v5.dxc.dxil`
- `shader_work\cache\hlsl\VS_0x5D918D91043B3ED0.translated.v5.dxc.hlsl`
- `shader_work\cache\d3d12\VS_0x5D918D91043B3ED0.translated.v5.dxc.dxil`
- `shader_work\cache\hlsl\PS_0xA4A965C189287B99.translated.v5.dxc.hlsl`
- `shader_work\cache\d3d12\PS_0xA4A965C189287B99.translated.v5.dxc.dxil`
- `shader_work\cache\hlsl\PS_0xC4ED2979F29C9139.translated.v5.dxc.hlsl`
- `shader_work\cache\d3d12\PS_0xC4ED2979F29C9139.translated.v5.dxc.dxil`

The PS lowering is intentionally limited, but it now uses four D3D12 texture/sampler bindings for the decoded fetch constants. D3D12 replay allocates four SRV/sampler descriptors per supported draw and binds the draw's texture fetch records in shader binding order, so this shader maps `tf4/tf3/tf2/tf1` to `t0/t1/t2/t3` and `s0/s1/s2/s3`. Unsupported shaders still fail closed before compilation.

The `xenos_limited_semantic_v6` cache version adds the next runtime post-process pair:

- `shader_work\cache\hlsl\VS_0x81311AC4B1FBD082.translated.v6.dxc.hlsl`
- `shader_work\cache\d3d12\VS_0x81311AC4B1FBD082.translated.v6.dxc.dxil`
- `shader_work\cache\hlsl\PS_0x246E20EF10E0DDC7.translated.v6.dxc.hlsl`
- `shader_work\cache\d3d12\PS_0x246E20EF10E0DDC7.translated.v6.dxc.dxil`

Both entries are non-diagnostic cache records produced by `native_shader_inspect.exe --compile-translated-hlsl-dxc`. The VS rule preserves captured vf95 position/UV data through the replay canonical input layout. The PS rule is partial: it proves D3D12 binding for the captured format-2 texture resources and captured constants, but does not yet lower the full predicated/scalar Xenos ALU. Replay draw `1013` on `sidecar_capture_002` writes `native-renderer-246e-draw1013-v6-d3d12-real.bmp` with SHA-256 `BA999F177A69BAA48BFB044B9B0142A05EF7EE6E013570F47EEF087FC04B0A70`.

Verified generated-pair replay:

```cmd
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-generated-v4-multitexture-d3d12-real.bmp --no-summary
```

Result: `5/5` supported draws for `VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139`, `20` captured texture SRVs, `20` captured sampler descriptors, captured render state from draw `748`, and output SHA-256 `AF172ED5685D34C32696219E3C9E8630CDB8085341613BEFDBA2E3F85EF86FEB` with the v5 cache.

Explicit non-indexed replay:

```cmd
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl --draw 25 --backend d3d12 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-ab1e-nonindexed-d3d12-real.bmp --no-summary
```

Result: one non-indexed `PM4_DRAW_INDX_2` draw for `VS=0xAB1E86137A0240E8 PS=0xA4A965C189287B99`, fallback white descriptors for the no-texture shader pair, default PSO state for non-indexed bring-up, and output SHA-256 `A20B0105C2961DEB0BD9AEFBD91CD34E69D483FACC52AB2AD037099DBE7B7EB8`.

Pair-specific PS interface update:

- PS `0xC4ED2979F29C9139` and PS `0x246E20EF10E0DDC7` are used with both indexed VS interfaces and the non-indexed `AB1E86137A0240E8` interface.
- Indexed pairs keep the older color+UV PS variants: `PS_0xC4ED2979F29C9139.translated.v5.dxc` and `PS_0x246E20EF10E0DDC7.translated.v6.dxc`.
- `AB1E86137A0240E8` pairs use the UV-only v7 PS variants because that VS exports `TEXCOORD0` but not `COLOR0`.
- Verified direct replays on `vertex_recapture_001`: draw `698` (`AB1E86/C4ED`) and draw `910` (`AB1E86/246E`) now both exit `0`, bind captured texture/sampler state, and submit real draws.
- Verified frame replay: `--frame 0 --backend d3d12 --skip-unsupported` submits `260/260` geometry-supported draws across `7` shader pairs. The remaining `2155` skipped draws have no captured vertex/fetch state.
- Vertexless B6/A4 cache update: `shader_work\cache\hlsl\VS_0xB6C9863F710683EC.vertexless.v8.dxc.hlsl` is a no-input `SV_VertexID` variant for the captured no-fetch point-list class `VS=0xB6C9863F710683EC` / `PS=0xA4A965C189287B99`. It is selected only for non-indexed primitive-type-`1` draws with no vertex/fetch records; the D3D12 backend compiles/caches it as `VS_0xB6C9863F710683EC.vertexless.v8.dxc`. Direct replay of draw `0` submits `2090/2090` draws for that pair, and frame skip replay on `vertex_recapture_001` now submits `2350` supported draws across `8` shader pairs with `3` no-side-effect draws elided and `62` enabled-color `DDED7E/3A6876` no-fetch point draws still unsupported.

Static extracted `.ucode` semantic decode is still blocked by file-layout ambiguity. Raw `.ucode` artifact commands still work, but tested static files include metadata/constants before the analyzer's expected payload range.

## Not implemented yet

- Proven runtime 64-bit shader hash to static container/microcode matching.
- Complete static-file Xenos shader disassembler.
- Complete executable backend-neutral shader IR for runtime shaders. A raw unresolved `bo2shaderir.raw_xenos.v1` JSON skeleton exists for static `.ucode`, and runtime `bo2shaderir.semantic_xenos.v1` inspection artifacts now exist, but they are not yet a complete HLSL-ready operation graph.
- Complete HLSL/SPIR-V generation.
- Broad real HLSL/SPIR-V generation from decoded Xenos operations. A diagnostic HLSL scaffold exists, and a very small `xenos_simple_passthrough_v1` translated HLSL subset exists, but most semantic Xenos instructions are not lowered.
- Full translated-shader DXC integration into replay/live backend shader selection. The inspect tool can now compile the limited translated subset to DXIL cache artifacts, but the D3D12 replay backend does not yet consume those automatic translated entries.
- Persistent compiled shader cache for broadly translated shaders. Diagnostic DXBC/DXIL cache paths and limited translated DXIL cache entries now exist.
- DXC/DXIL compiler integration for full semantic Xenos lowering.
- Cache entries for all runtime-used automatically translated Xenos shaders.

## Current matching rule

Runtime replay hashes are treated as distinct runtime IDs. They are not assumed to equal static SHA-256 container or microcode hashes. The current direct substring rule finds `0/8` runtime shader hashes in `shader_work/shaders/index.json`, and the reproducible PM4 payload rules also find `0/8` across raw LE, raw BE, trailing-zero-trimmed LE, and trailing-zero-trimmed BE SHA-256 variants. Matching must now use aligned/padded payload variants, shader record metadata, container headers, Ghidra-backed material records, or manual mapping evidence.
