# Native Renderer Function Map

Evidence date: 2026-06-28

Ghidra programs loaded:

- `CoDMPServer_PC.exe`: Windows/server executable with PDB symbols.
- `default.xex`: Xbox 360 XEX.

Generated-code references are from `default/generated` and `default_mp/generated`.

| Windows function | XEX function/address | Confidence | Evidence | Guessed signature | Notes |
|---|---:|---|---|---|---|
| `R_IssueRenderCommands` `0x00A2F1E0` | Not mapped yet | Low | Windows decompile sets `frontEndDataOut->drawType`, calls `RB_BeginFrame`, `RB_Draw3D`, `RB_CallExecuteRenderCommands`, and `RB_EndFrame`. XEX strings are stripped and no equivalent was proven. | `void(uint32_t drawType)` | Primary frame-loop target. Needs callgraph matching around render-thread sync and backend command execution. |
| `RB_BeginFrame` `0x00A5C030` | Not mapped yet | Low | Windows decompile increments backend frame count, clears render target state, updates cinematic frame, resets tess counts, and starts GPU timers. | `void(GfxBackEndData*)` | Use globals and frame counter access patterns to find XEX equivalent. |
| `RB_EndFrame` `0x00A5A750` | Present path candidate: `default` `sub_8257E590`; `default_mp` `sub_821211A0` | Medium for present, low for full function | Windows `RB_EndFrame` calls `R_GPU_EndFrame` and `RB_SwapBuffers`. Generated XEX `sub_8257E590` and `sub_821211A0` call `__imp__VdSwap` with ring-buffer, fetch, frontbuffer, width, and height arguments. | `void(uint32_t drawType)` for Windows; XEX caller likely takes a device/context pointer | The candidate is the XEX present/swap helper, not proven to cover all Windows `RB_EndFrame` work. |
| `RB_SwapBuffers` `0x00A5A510` | `default` `sub_8257E590`; `default_mp` `sub_821211A0` | Medium | Generated XEX callers build `VdSwap` arguments, call `__imp__VdSwap`, then advance frontbuffer/ring-buffer state. ReXGlue SDK documents `VdSwap` as the emulated swap trigger. | XEX: likely `void(DeviceContext*)` | First native hook is installed at `__imp__VdSwap` rather than replacing this whole caller. |
| `R_DrawIndexedPrimitive` `0x00A8DB70` | XEX packet emitters: `0x825828D8`, `0x82582A30`, `0x8258A5E8`, `0x8258CF68`, range around `0x82596440` | Medium for draw packet emission, low for exact one-to-one PC function | Windows decompile gates by draw-prim dvars, tracks primitive stats, flushes constant buffers, then calls D3D11 device-context vtable offset `0x30` with `triCount * 3`, `baseIndex`, and `0`. XEX candidates emit `PM4_DRAW_INDX_2` (`0xC0003600`) packets. | PC: `void(GfxCmdBufState*, GfxCmdBufPrimState*, GfxDrawPrimArgs*)`; XEX emitters take the command-buffer/state object in `r3` and draw args in later registers depending on helper | These are the native-renderer draw interception targets. `0x8258CF68` is the cleanest variable-count draw helper. |
| `R_FlushDirtyConstantBuffers` `0x00A8C670` | XEX candidates `0x8258CE40`, `0x82597F50` | Medium | Called immediately before the Windows D3D indexed draw. XEX `0x8258CE40` emits `PM4_SET_SHADER_CONSTANTS` (`0xC0065600`-style), while `0x82597F50` emits `PM4_LOAD_ALU_CONSTANT` (`0xC0022F00`). | PC: `void(GfxCmdBufPrimState*)`; XEX: command-buffer state plus constant source pointer/range | Useful anchors for shader constants and uniform uploads. |
| `Material_LoadPass` `0x00A4BC10` approx. | XEX runtime binding candidates `0x82597CA8`, `0x82597DF8`, `0x82598140`; static asset-load equivalent still unmapped | Medium for runtime shader binding, low for asset loader | Windows decompile calls `Material_LoadPassVertexShader`, `Material_LoadPassPixelShader`, sorts shader args, counts update frequencies, allocates pass args, and validates linkage. XEX ranges compare shader records, emit `PM4_IM_LOAD_IMMEDIATE` / `PM4_IM_LOAD`, and upload constants. | PC loader signature unknown; XEX runtime binder uses command-buffer state plus shader/material records | XEX has no matching token strings in Ghidra/generated output. The packet emitters are runtime binding paths, not proven fastfile material parser functions. |
| `Material_LoadPassVertexShader` `0x00A4B730` | Not mapped yet | Low | Windows parses `vertexShader`, reads version/name, calls `Material_RegisterVertexShader`, and maps shader arguments. | `bool(char**, uint16_t*, ShaderParameterSet*, MaterialPass*, uint32_t, uint32_t*, MaterialShaderArgument*)` | String stripping prevents direct anchor matching in XEX. |
| `Material_LoadPassPixelShader` `0x00A4B8A0` | Not mapped yet | Low | Windows parses `pixelShader`, substitutes `black.hlsl` for connect-only paths, registers the pixel shader, and maps shader arguments. | Same shape as vertex loader | Pair this with `shader_work` hashes and runtime material pass logging. |
| `Image_Create2DTexture_PC` `0x00A60540` | Not applicable directly | Medium for non-equivalence | Windows path creates D3D11 2D textures and shader-resource views. XEX uses Xbox texture/fetch constants and ReXGlue maps them through Xenos texture cache. | `void(GfxImage*, uint16_t width, uint16_t height, int levels, int flags, DXGI_FORMAT, void*)` | Native backend should translate Xenos texture fetch/resource state, not copy the PC D3D11 creation path literally. |
| Xbox `VdSetSystemCommandBufferGpuIdentifierAddress` import | `default` `0x826EAF4C`; `default_mp` `0x827FBA14` | High for import address | Generated import tables map these addresses to `__imp__VdSetSystemCommandBufferGpuIdentifierAddress`. Ghidra xref from `Function_82588DE8` confirms a direct call in `default.xex`. | `void(mapped_void)` | Hooked for logging command-buffer identity changes. |
| Xbox `VdSwap` import | `default` `0x826EAF9C`; `default_mp` `0x827FBB94` | High for import address | Generated import tables map these addresses to `__imp__VdSwap`. Generated call sites pass ring buffer, texture fetch, frontbuffer pointer, texture format, color space, width, and height. ReXGlue SDK `VdSwap_entry` writes a Xenia-specific `PM4_XE_SWAP` packet. | `void(mapped_void buffer, mapped_void fetch, mapped_void unk2, mapped_void syscmd, mapped_void token, mapped_u32 frontbuffer, mapped_u32 format, mapped_u32 colorSpace, mapped_u32 width, mapped_u32 height)` | First implemented runtime intercept. |
| Xbox present command packet | `PM4_XE_SWAP` written by ReXGlue after XEX `VdSwap` call | High for packet shape | ReXGlue `VdSwap_entry` zeroes 64 dwords at the passed ring-buffer pointer, writes a fetch-constant packet, then writes `PM4_XE_SWAP` with `kSwapSignature`, physical frontbuffer address, width, and height. The native renderer now snapshots this buffer after forwarding `VdSwap`. | Packet payload: `SWAP`, `frontbufferPhysical`, `width`, `height` | This is still a present-path trace, not draw translation. It gives a concrete packet parser entry point for later native backend work. |

## Immediate mapping gaps

- Full XEX equivalents for `R_IssueRenderCommands`, `RB_BeginFrame`, `RB_EndFrame`, `R_DrawIndexedPrimitive`, and `Material_LoadPass*` are still unproven.
- The loaded XEX has no useful renderer/material/shader strings for direct string-anchor matching.
- Current strongest XEX anchors are generated import call sites, Ghidra xrefs around Xbox video functions, and the ReXGlue-written `PM4_XE_SWAP` packet captured after `VdSwap`.

## Generated PM4 Packet Candidates

These are not yet proven Windows-to-XEX function equivalents, but they are strong runtime hook candidates because generated XEX code constructs known Xenos Type-3 packet headers. Packet words are built as `0xC0000000 | ((count - 1) << 16) | (opcode << 8)`.

| Candidate | Packet evidence | Confidence | Notes |
|---|---|---|---|
| `default` `sub_825828D8`; `default_mp` `sub_82117BC8` | Generated code builds a Type-3 packet with low bits `0x3600`, matching `PM4_DRAW_INDX_2`. | Medium | Likely an auto-index draw helper. Needs runtime argument logging before replacing or forwarding. |
| `default` `sub_82582A30` at `loc_82582C84`; `default_mp` `sub_82117D20` at `loc_82117F74` | Generated code writes `lis -16384` then `ori 0x3600`, followed by initiator payload stores. | Medium | Runtime logger installed. It snapshots command-buffer writes from object offset `48`, parses `PM4_DRAW_INDX_2`, logs index count, primitive type, source select, and index-size flag, then submits a backend-neutral `DrawIndexed` command. Windows hosts attempt a generated-function trampoline with an expected-prologue check; non-Windows hosts still need lower-level generated-call interception before direct calls are guaranteed to fire. |
| `default` `sub_8258A5E8` at `loc_8258A6BC`; `default_mp` `sub_8212E280` at `loc_8212E9CC` | Generated code writes `PM4_DRAW_INDX_2`, then `0x00030088`-style initiator data and several zero/index payload dwords. | Medium | Looks like a clear packetized draw emit path, but call-site role is not yet mapped to a named Windows renderer function. |
| `default` `sub_8258CF68` at `loc_8258CF98`; `default_mp` `sub_8212EB40` at `loc_8212F030` | Generated code writes `PM4_DRAW_INDX_2` and computes part of the payload from runtime values. | Medium | Candidate for a variable-count draw helper. Compare with Windows `R_DrawIndexedPrimitive` once runtime arguments are logged. |
| `default` `sub_8258CCE8`; `default_mp` `sub_8212D478` | Generated code constructs `0x2B00`, matching `PM4_IM_LOAD_IMMEDIATE`; Ghidra also finds `ori ... 0x2b00` at SP addresses including `0x8257D3A8`, `0x82589A20`, `0x82594474`, and `0x82597E68`. | Medium for shader-load packet emission | Likely shader microcode upload/setup path. This should be paired with `shader_work` hashes before native shader replacement. |

## XEX graphics function inventory

Ghidra MCP pass: 2026-06-28, program `default.xex`.

The XEX is stripped and has no useful `shader`, `material`, `pixel`, `vertex`, `render`, `draw`, `image`, or `texture` strings. Ghidra also misses or truncates several PowerPC functions that use save/restore thunks, so the most reliable evidence is packet construction in disassembly. Packet words use `0xC0000000 | ((count - 1) << 16) | (opcode << 8)`.

Replay follow-up MCP check: 2026-06-28. `CoDMPServer_PC.exe` is the default loaded MCP program and has the PDB-backed renderer symbols. Explicit program `default.xex` exposes the stripped XEX function table. MCP instruction searches in `default.xex` re-confirmed `PM4_DRAW_INDX_2` (`ori ..., 0x3600`) at `0x825829AC`, `0x82582C8C`, `0x8258A6C4`, `0x8258CFA0`, and `0x82596440`; `PM4_IM_LOAD_IMMEDIATE` (`ori ..., 0x2B00`) at `0x8257D3A8`, `0x82582948`, `0x82582A84`, `0x82582AE0`, `0x82589A20`, `0x82589A64`, `0x8258CD20`, `0x82594474`, `0x825944D4`, and `0x82597E68`; and `PM4_SET_SHADER_CONSTANTS` (`ori ..., 0x5600`) at `0x8258CE80`. `0x825828D8` decompiles as `xex_render_draw_autoindex_shader_bootstrap_candidate`; the other thunked starts force-decompile as save/restore placeholders, so generated C++ plus instruction-level evidence remains authoritative there.

### High-confidence draw emitters

| XEX function/range | Evidence | Likely role | Native renderer priority |
|---|---|---|---|
| `0x825828D8` `xex_render_draw_autoindex_shader_bootstrap_candidate` | Ghidra-created function decompiles cleanly. Emits `0xC0003B00` (`PM4_INVALIDATE_STATE`), `0xC0102B00` (`PM4_IM_LOAD_IMMEDIATE`), then `0xC0003600` (`PM4_DRAW_INDX_2`) with initiator `0x00010081`. | Shader bootstrap plus auto-index draw setup. | High. Good combined shader/draw logger target. |
| `0x82582A30` / draw packet at `0x82582C8C` | Disassembly emits shader immediate-load packets and then `PM4_DRAW_INDX_2` at `0x82582C8C`. Ghidra cannot form a valid full function body because the PPC prologue thunk is mis-modeled, but generated C++ exposes `sub_82582A30`. | Larger immediate shader + draw setup helper. | High. Hook installed for dispatcher calls and Windows generated-function detours. |
| `0x8258A5E8` / packet at `0x8258A6C4` | Allocates a `0x2000` byte buffer, calls helpers at `0x825899F8` and `0x82589CE8`, then writes repeated `PM4_DRAW_INDX_2` packets in a loop (`CTR = 0x18`) with initiator `0x00010081`. | Batched/clear-style draw packet builder or generated repeated primitive pass. | High for runtime logging; medium for direct replacement. |
| `0x8258CF68` / packet at `0x8258CFA0` | Emits `PM4_DRAW_INDX_2`, builds the initiator from `r4` and `r5`, stores the packet, then advances command-buffer offset `0x30`. | Clean variable-count indexed draw helper. | Highest priority draw hook candidate. |
| Range around `0x82596440` | Emits `PM4_DRAW_INDX_2`, initiator includes `r29 << 16`, writes additional payload words and may call `0x82583E98` with rectangle/viewport-like values. Ghidra has no containing function object. | Specialized draw path, possibly screen-space/quad/resolve style. | Medium. Log after the generic variable draw helper. |

### Shader and material binding emitters

| XEX function/range | Evidence | Likely role | Native renderer priority |
|---|---|---|---|
| `0x8258CCE8` | Parameterized `PM4_IM_LOAD_IMMEDIATE`; count derives from `r5`, source copied from `r4`, target/stage value comes through `r6`; reserves command-buffer space through `0x8257AD38`. | Shader microcode immediate upload helper. | Highest priority shader hook. |
| `0x82597DF8` / packet at `0x82597E68` | Compares shader/state records, emits `PM4_INVALIDATE_STATE`, `PM4_IM_LOAD_IMMEDIATE`, copies microcode bytes, then updates cached state. | Runtime shader binding/update path. | High, but probably too broad for replacement until arguments are logged. |
| `0x82598140` / packet constant at `0x825981BC` | Builds `PM4_IM_LOAD` (`0xC0012700`) and manipulates material/shader state flags under the command-buffer state object. | Pointer-based shader instruction load / material state binder. | Medium-high. Use runtime logs before patching. |
| `0x8257D320`, `0x8257DD60`, `0x82589A10`, `0x82594460`, `0x82595FDC`, `0x8259C618` | Aligned `PM4_INVALIDATE_STATE` and/or `PM4_IM_LOAD_IMMEDIATE` hits; not all decompiled in this pass. | Additional shader/state invalidation and immediate-load emitters. | Medium. Keep in packet scan watchlist. |

### Shader constant and uniform emitters

| XEX function/range | Evidence | Likely role | Native renderer priority |
|---|---|---|---|
| `0x8258CE40` / packet at `0x8258CE80` | Emits `PM4_SET_SHADER_CONSTANTS`; payload copies words from the source pointer in `r5`, derives destination from `r4 + 0xC00`, and advances command-buffer offset `0x30`. | Main shader constant upload helper. | High. Pair with draw logger to capture constant state before draws. |
| `0x82597F50` / packet at `0x82597FC4` | Iterates constant records and emits `PM4_LOAD_ALU_CONSTANT`; packet payload includes GPU constant address and count. | ALU/uniform constant upload from material/runtime table. | High. |
| `0x8222CD34`, `0x8222CD88`, `0x8222E0A4`, `0x8222E204` | Aligned `PM4_SET_CONSTANT` byte-pattern candidates away from the main renderer range; not yet proven with disassembly. | Possible fixed register/constant emitters or false positives. | Low until inspected. |
| `0x8240D878` | Aligned `PM4_LOAD_CONSTANT_CONTEXT` candidate. | Possible context constant load. | Low until inspected. |

False positives removed from the constant list: `0x82503178`, `0x82507C7C`, and `0x82507FD4` are address/offset constants in math code, not PM4 packets.

### Command stream, sync, events, and queries

| XEX function/range | Evidence | Likely role | Native renderer priority |
|---|---|---|---|
| `0x8257AB00` | Called before many packet writes when command-buffer write pointer at object offset `0x30` exceeds limit at `0x38`. | Command-buffer grow/flush/submit helper. | Very high for tracing complete command streams. |
| `0x8257AD38` | Called by packet emitters before variable-sized writes, returns writable command-buffer pointer or null. | Command-buffer reserve helper. | Very high for generic instrumentation. |
| `0x82579D5C`, `0x8258A54C`, `0x82591CE8` | Aligned `PM4_INDIRECT_BUFFER` candidates. | Indirect-buffer submission or nested command buffer dispatch. | High for full command capture. |
| `0x8257D10C`, `0x82582E58`, `0x82583064`, `0x82594900`, `0x82595F7C`, `0x82596BF8`, `0x825999AC` | Aligned `PM4_EVENT_WRITE` candidates. | GPU events, cache flushes, visibility/timing/writeback. | Medium-high. Needed for synchronization correctness. |
| `0x8269E384`, `0x8269EA68`, `0x8269EB18`, `0x8269EB5C`, `0x8269EC40`, `0x8269FEC4`, `0x8269FF8C`, `0x826A0030`, `0x826A0218`, `0x826A0400`, `0x826A0508`, `0x826A08AC`, `0x826A090C`, `0x826A0DB0`, `0x826A0DFC` | Aligned `PM4_VIZ_QUERY` candidates clustered in a late code/import-helper range. | Visibility/occlusion query path or helper-table constants. | Medium until disassembled. |
| `0x82577644`, `0x82578714`, `0x82579F10`, `0x82579FD0`, `0x8257A1CC`, `0x8257E13C`, `0x8257E248`, `0x8257E410`, `0x8257E4E4`, `0x8257FA78`, `0x82582818`, `0x82583564`, `0x82583994`, `0x82595A20`, `0x82596638`, `0x82597134`, `0x82599A98` | Aligned `PM4_WAIT_REG_MEM` candidates. | GPU wait/fence/cache synchronization. | Medium. Important for correctness, not first draw translation. |
| `0x82599FF8` | Aligned `PM4_WAIT_FOR_IDLE` candidate. | Hard GPU idle/sync point. | Medium. |
| `0x82599A44`, `0x82591E04` | Aligned `PM4_MEM_WRITE` / `PM4_REG_TO_MEM` candidates. | GPU writeback/counter/readback. | Medium-low. |

### Present and Xbox video imports

| XEX address | Evidence | Likely role | Native renderer priority |
|---|---|---|---|
| `0x8257E590` | Present/swap helper candidate; disassembly copies render-target/fetch state and eventually follows the Xbox video path. Existing generated/rexglue notes identify it as the SP helper that calls `VdSwap`. | XEX present/swap-buffer wrapper. | High for frame boundary and backbuffer capture. |
| `0x826EAF9C` | ReXGlue generated import slot for `__imp__VdSwap`; existing hook logs `VdSwap` arguments and the ReXGlue-written `PM4_XE_SWAP` packet. | Swap/present import. | Already hooked. |
| `0x826EAF4C` | ReXGlue generated import slot for `__imp__VdSetSystemCommandBufferGpuIdentifierAddress`; existing hook records command-buffer GPU identifier address. | Command-buffer identity/config import. | Already hooked. |

### PC/PDB reference anchors used

Important PC renderer symbols from `CoDMPServer_PC.exe`:

- Frame/backend: `R_IssueRenderCommands` `0x00A2F1E0`, `RB_BeginFrame` `0x00A5C030`, `RB_Draw3D` `0x00A5A9D0`, `RB_Draw3DInternal` `0x00A28130`, `RB_CallExecuteRenderCommands` `0x00A5AB50`, `RB_EndFrame` `0x00A5A750`, `RB_SwapBuffers` `0x00A5A510`.
- Draw/tessellation: `R_DrawIndexedPrimitive` `0x00A8DB70`, `R_FlushDirtyConstantBuffers` `0x00A8C670`, `RB_BeginSurface` `0x00A64D20`, `RB_DrawTessSurface` `0x00A64E80`, `RB_EndTessSurface` `0x00A64FD0`, `RB_SetTessTechnique` `0x00A65060`.
- Material/shader loading: `Material_LoadPass` `0x00A4BC10`, `Material_LoadPassVertexShader` `0x00A4B730`, `Material_LoadPassPixelShader` `0x00A4B8A0`, `Material_RegisterVertexShader` `0x00A4AF70`, `Material_RegisterPixelShader` `0x00A4B0F0`, `Material_SetPassShaderArguments_DX` `0x00A4B490`, `Material_ParseShaderArguments` `0x00A4A6B0`.
- Post/render passes: `RB_StandardRenderCommands` `0x00A269B0`, `RB_StandardDrawCommands` `0x00A27AE0`, `RB_DrawLitCommandBuffer` `0x00A26CB0`, `RB_DrawDepthPrepassCommandBuffer` `0x00A969C0`, `RB_SunShadowMaps` `0x00A95C40`, `RB_SpotShadowMaps` `0x00A96460`.

MCP decompilation rechecked the key PC reference on 2026-06-28: `R_DrawIndexedPrimitive` gates draw-prim dvars, calls `RB_TrackDrawPrimCall`, calls `R_FlushDirtyConstantBuffers`, then dispatches through the D3D11 device-context vtable with `triCount * 3`, `baseIndex`, and vertex offset `0`. `R_FlushDirtyConstantBuffers` iterates four dirty constant buffers, maps each D3D11 constant buffer, copies the dirty payload, unmaps it, and clears the dirty flag.

### Instrumentation notes

- Source hooks now log `VdSwap`, `VdSetSystemCommandBufferGpuIdentifierAddress`, SP draw packet emitters `0x825828D8`, `0x82582A30`, `0x8258A5E8`, `0x8258CF68`, SP shader/material emitters `0x8258CCE8`, `0x8258CE40`, `0x82597DF8`, `0x82597F50`, `0x82598140`, and SP command-buffer grow/reserve helpers `0x8257AB00` / `0x8257AD38`.
- `default_mp` also logs the known matching packet emitters `0x82117BC8`, `0x82117D20`, `0x8212D478`, `0x8212E280`, and `0x8212EB40`.
- The generated C++ tree contains direct symbols for the high-priority SP candidates listed above. The hooks save original PPC functions from the dispatcher, then install generated-function detours on Windows hosts so direct generated calls are captured there too.
- ReXGlue command-processor callbacks now provide a second, lower-level capture path that does not depend on individual XEX function detours. Runtime events from `default.exe` in `native` mode proved live `PM4_IM_LOAD_IMMEDIATE`, `PM4_LOAD_ALU_CONSTANT`, `PM4_DRAW_INDX`, `PM4_DRAW_INDX_2`, and `PM4_XE_SWAP` traffic.
- The 2026-06-28 bounded capture `native-renderer-capture-limit.jsonl` contains `20000` complete JSONL events, including `4388` draw packets, `1011` shader loads, `170` constant uploads, and `85` swaps. Representative observed shader hashes include `B6C9863F710683EC`, `A4A965C189287B99`, `1E6883FCCDE1F688`, `AB1E86137A0240E8`, `81311AC4B1FBD082`, and `246E20EF10E0DDC7`.
- Observed indexed draw example: `PM4_DRAW_INDX` with `indices=6`, `prim=4`, `src=0`, `indexed=true`, `index_base=0x04fa0770`, `index_len=12`, `index_format=0`, `index_endian=1`, VS `81311AC4B1FBD082`, PS `246E20EF10E0DDC7`.
- Observed auto-index draw examples: `PM4_DRAW_INDX_2` with `indices=1`, `prim=1`, `src=2` and repeated `PM4_DRAW_INDX_2` with `indices=3`, `prim=8`, `src=2`.
- Observed constant upload examples: `PM4_LOAD_ALU_CONSTANT` with `address=0x06019BC0`, `offset_type=0x000007F0`, `index=2032`, `dwords=16`; `address=0x06019940`, `offset_type=0x000003F0`, `index=1008`, `dwords=16`; and `address=0x06011400`, `offset_type=0x000007A0`, `index=1952`, `dwords=16`.
- Observed swap examples alternate frontbuffers `0x1E0E8000` and `0x1DD38000` at `1280x720`.
- Remaining risk: Android/ARM64 still has dispatcher replacement only. Direct generated calls that bypass the dispatcher need an ARM64-safe generated-function detour or generated-call rewrite before every packet emitter is guaranteed to log on Android.
