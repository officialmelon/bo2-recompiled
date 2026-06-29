# Native Renderer Shader Cache

Last updated: 2026-06-29

## Implemented

`native_shader_inspect.exe` has been added as the first shader registry inspection tool.

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
- SHA-256 of raw PM4 payload dwords did not directly match `shader_work/shaders/index.json` for little-endian or big-endian byte order for runtime hashes `0x5D918D91043B3ED0`, `0xC4ED2979F29C9139`, `0xB6C9863F710683EC`, or `0xA4A965C189287B99`.

## Not implemented yet

- Proven runtime 64-bit shader hash to static container/microcode matching.
- Xenos shader disassembler.
- Backend-neutral shader IR.
- HLSL/SPIR-V generation.
- DXC integration.
- Persistent compiled shader cache.
- Manual override table.

## Current matching rule

Runtime replay hashes are treated as distinct runtime IDs. They are not assumed to equal static SHA-256 container or microcode hashes. The current direct substring rule finds `0/8` runtime shader hashes in `shader_work/shaders/index.json`, and direct SHA-256 of captured PM4 payload bytes does not match the static index for the tested top/target shaders. Matching must now use stripped/aligned/padded payload variants, shader record metadata, container headers, Ghidra-backed material records, or manual mapping evidence.
