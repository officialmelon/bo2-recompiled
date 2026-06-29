# Native Renderer Shader Cache

Last updated: 2026-06-29

## Implemented

`native_shader_inspect.exe` has been added as the first shader registry inspection tool.

A first manual D3D12 override pair exists for the draw `1209` runtime shader hashes:

- Manifest: `shader_work\native_overrides\overrides.json`
- Vertex override: `shader_work\native_overrides\d3d12\vs_5D918D91043B3ED0.hlsl`
- Pixel override: `shader_work\native_overrides\d3d12\ps_C4ED2979F29C9139.hlsl`

The D3D12 replay backend currently resolves overrides by deterministic runtime-hash filenames under `shader_work\native_overrides\d3d12` and compiles them through `D3DCompile` at replay time. The manifest is not parsed yet, and compiled DXIL is not persisted yet.

Current supported commands:

```powershell
native_shader_inspect.exe --index shader_work\shaders\index.json --summary
native_shader_inspect.exe --index shader_work\shaders\index.json --hash <hash-or-substring> --find
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\vertex_fetch_capture_001\events.jsonl --list-runtime-shaders --top-shaders 20
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\vertex_fetch_capture_001\events.jsonl --match-runtime-shaders --top-shaders 20
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\shader_payload_capture_001\events.jsonl --list-runtime-shaders --match-runtime-shaders --top-shaders 20
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

## Not implemented yet

- Proven runtime 64-bit shader hash to static container/microcode matching.
- Xenos shader disassembler.
- Backend-neutral shader IR.
- HLSL/SPIR-V generation.
- DXC integration.
- Persistent compiled shader cache.
- Parsed manual override table and persistent compiled override cache.

## Current matching rule

Runtime replay hashes are treated as distinct runtime IDs. They are not assumed to equal static SHA-256 container or microcode hashes. The current direct substring rule finds `0/8` runtime shader hashes in `shader_work/shaders/index.json`, and the reproducible PM4 payload rules also find `0/8` across raw LE, raw BE, trailing-zero-trimmed LE, and trailing-zero-trimmed BE SHA-256 variants. Matching must now use aligned/padded payload variants, shader record metadata, container headers, Ghidra-backed material records, or manual mapping evidence.
