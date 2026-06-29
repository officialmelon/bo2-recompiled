# Native Renderer Shader Cache

Last updated: 2026-06-28

## Implemented

`native_shader_inspect.exe` has been added as the first shader registry inspection tool.

Current supported commands:

```powershell
native_shader_inspect.exe --index shader_work\shaders\index.json --summary
native_shader_inspect.exe --index shader_work\shaders\index.json --hash <hash-or-substring> --find
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\vertex_fetch_capture_001\events.jsonl --list-runtime-shaders --top-shaders 20
native_shader_inspect.exe --index shader_work\shaders\index.json --capture native_captures\vertex_fetch_capture_001\events.jsonl --match-runtime-shaders --top-shaders 20
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

## Not implemented yet

- Proven runtime 64-bit shader hash to static container/microcode matching.
- Xenos shader disassembler.
- Backend-neutral shader IR.
- HLSL/SPIR-V generation.
- DXC integration.
- Persistent compiled shader cache.
- Manual override table.

## Current matching rule

Runtime replay hashes are treated as distinct runtime IDs. They are not assumed to equal static SHA-256 container or microcode hashes. The current direct substring rule finds `0/8` runtime shader hashes in `shader_work/shaders/index.json`. Matching must be proven with captured PM4 shader payloads, byte-swapped payload hashes, shader record metadata, or manual mapping evidence.
