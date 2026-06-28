# Native Renderer Shader Cache

Last updated: 2026-06-28

## Implemented

`native_shader_inspect.exe` has been added as the first shader registry inspection tool.

Current supported commands:

```powershell
native_shader_inspect.exe --index shader_work\shaders\index.json --summary
native_shader_inspect.exe --index shader_work\shaders\index.json --hash <hash-or-substring> --find
```

Verified summary:

- Zones: `142`
- Occurrences: `36769`
- Unique containers: `33950`
- Pixel containers: `31134`
- Vertex containers: `2816`
- Unique microcode programs: `33355`
- Invalid bounds: `0`

## Not implemented yet

- Runtime 64-bit shader hash to static container/microcode matching.
- Xenos shader disassembler.
- Backend-neutral shader IR.
- HLSL/SPIR-V generation.
- DXC integration.
- Persistent compiled shader cache.
- Manual override table.

## Current matching rule

Runtime replay hashes are treated as distinct runtime IDs. They are not assumed to equal static SHA-256 container or microcode hashes. Matching must be proven with captured PM4 shader payloads, byte-swapped payload hashes, shader record metadata, or manual mapping evidence.
