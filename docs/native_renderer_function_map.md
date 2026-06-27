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
| `R_DrawIndexedPrimitive` `0x00A8DB70` | Not mapped yet | Low | Windows decompile gates by draw-prim dvars, tracks primitive stats, flushes constant buffers, then calls D3D11 device-context vtable offset `0x30` with `triCount * 3`, `baseIndex`, and `0`. | `void(GfxCmdBufState*, GfxCmdBufPrimState*, GfxDrawPrimArgs*)` | Next draw-call logging target. Look for XEX functions that emit index-count PM4 draw packets after constant-buffer flush equivalents. |
| `R_FlushDirtyConstantBuffers` `0x00A8C670` | Not mapped yet | Low | Called immediately before the Windows D3D indexed draw. | `void(GfxCmdBufPrimState*)` | Useful anchor for shader constants and uniform uploads. |
| `Material_LoadPass` `0x00A4BC10` approx. | Not mapped yet | Low | Windows decompile calls `Material_LoadPassVertexShader`, `Material_LoadPassPixelShader`, sorts shader args, counts update frequencies, allocates pass args, and validates linkage. | `bool(char** parse, uint16_t* state, MaterialPass*, MaterialStateMap**, int materialType)` | XEX has no matching token strings in Ghidra/generated output. Use asset structure layout and calls to material arg helpers next. |
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
| `default` `sub_82582A30` at `loc_82582C84`; `default_mp` `sub_82117D20` at `loc_82117F74` | Generated code writes `lis -16384` then `ori 0x3600`, followed by initiator payload stores. | Medium | Runtime logger installed. It snapshots command-buffer writes from object offset `48`, parses `PM4_DRAW_INDX_2`, and logs index count, primitive type, source select, and index-size flag. Windows hosts attempt a generated-function trampoline with an expected-prologue check; non-Windows hosts still need lower-level generated-call interception before direct calls are guaranteed to fire. |
| `default` `sub_8258A5E8` at `loc_8258A6BC`; `default_mp` `sub_8212E280` at `loc_8212E9CC` | Generated code writes `PM4_DRAW_INDX_2`, then `0x00030088`-style initiator data and several zero/index payload dwords. | Medium | Looks like a clear packetized draw emit path, but call-site role is not yet mapped to a named Windows renderer function. |
| `default` `sub_8258CF68` at `loc_8258CF98`; `default_mp` `sub_8212EB40` at `loc_8212F030` | Generated code writes `PM4_DRAW_INDX_2` and computes part of the payload from runtime values. | Medium | Candidate for a variable-count draw helper. Compare with Windows `R_DrawIndexedPrimitive` once runtime arguments are logged. |
| `default` `sub_8258CCE8`; `default_mp` `sub_8212D478` | Generated code constructs `0x2B00`, matching `PM4_IM_LOAD_IMMEDIATE`; Ghidra also finds `ori ... 0x2b00` at SP addresses including `0x8257D3A8`, `0x82589A20`, `0x82594474`, and `0x82597E68`. | Medium for shader-load packet emission | Likely shader microcode upload/setup path. This should be paired with `shader_work` hashes before native shader replacement. |
