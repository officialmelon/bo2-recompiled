# Native Renderer Backend Plan

Last updated: 2026-06-28

This plan describes the path from the current capture/replay milestone to a BO2-owned native renderer. It does not claim that native rendering is complete. The current executable backend split is explicit: `null` is analysis-only, `d3d12-diagnostic` is synthetic debug visualization, and `d3d12` is reserved for real resource-backed rendering and currently fails closed.

## Current state

- Runtime hooks and the ReXGlue command-processor trace sink capture PM4 packet, shader, constant, draw, swap, and present events.
- `native_render_replay.exe` parses verified captures and reconstructs draw state with bound VS/PS hashes, recent constants, and current bound constant ranges.
- `native_render_replay.exe --backend d3d12-diagnostic` now creates a BO2-owned D3D12 device, renders an offscreen debug target from replayed draw events, submits a synthetic triangle draw pass through a real D3D12 shader pipeline, reads it back, and writes a BMP.
- `native_render_replay.exe --backend d3d12` refuses to draw synthetic output and reports missing real BO2 resource/shader state.
- The first verified replay run parsed `20000` events, `4388` draws, `1011` shader loads, `170` constant uploads, `85` PM4 swaps, and `86` present snapshots with `0` parse errors.
- The first verified D3D12 clear-tile replay output is `native-renderer-d3d12-replay.bmp`, size `3686454`, SHA-256 `D82EED84E69EED945B8DA0FF70D7F97B80FCCB35392D5E1E822809231DA03E37`.
- The first verified D3D12 shader-pipeline geometry replay output is `native-renderer-d3d12-geometry-replay.bmp`, size `3686454`, SHA-256 `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`.
- The strongest mapped XEX draw entry remains `0x8258CF68` for variable `PM4_DRAW_INDX_2`; the broadest runtime source remains the CP trace sink because it sees packets after all emitter paths.
- PC reference from `CoDMPServer_PC.exe` with PDB: `R_DrawIndexedPrimitive` flushes dirty constant buffers and calls the D3D11 draw entry with `triCount * 3`, `baseIndex`, and vertex offset `0`.

## Backend-neutral contract

The native backend should consume a normalized stream, not raw JSON:

1. `BeginReplayFrame` / `EndReplayFrame` with a present target.
2. `BindShader(stage, hash, guest_address, microcode_size)`.
3. `UploadConstants(type, index, address, dword_count)`.
4. `SetRenderTargets(color, depth, dimensions, formats)`.
5. `BindVertexFetch/IndexBuffer` from Xenos fetch constants and PM4 draw source.
6. `DrawIndexed` for `PM4_DRAW_INDX`.
7. `DrawAutoIndexed` for `PM4_DRAW_INDX_2` / `source_select=2`.
8. `Present(frontbuffer, width, height)`.

The replay tool now reconstructs items 1, 2, 3, 6, 7, and 8 enough to print state. Item 3 can now carry optional raw dwords, but the existing verified capture predates that payload. Items 4 and 5 need additional capture fields before a real backend can draw BO2 geometry correctly.

## D3D12 first backend

D3D12 is the first practical backend target on this machine because the Windows build already has the SDK/compiler environment and ReXGlue is configured with `REX_HAS_D3D12=1`.

Proposed source layout:

- `src/native_renderer/backend_d3d12/D3D12RendererBackend.h`
- `src/native_renderer/backend_d3d12/D3D12RendererBackend.cpp`
- `src/native_renderer/backend_d3d12/D3D12ReplayBackend.h`
- `src/native_renderer/backend_d3d12/D3D12ReplayBackend.cpp`

Keep the runtime backend and replay backend sharing state translation code, but keep window/swapchain ownership separate:

- Runtime backend: receives events through `RendererBackend` from `default.exe`.
- Replay backend: receives `ReplayCapture`/`ReplayDrawState` from `native_render_replay.exe`.

Current implemented D3D12 diagnostic replay behavior:

- Creates a D3D12 device, direct queue, command allocator/list, render target, RTV heap, readback buffer, and fence.
- Chooses output dimensions from the first present/swap event, currently `1280x720` for the verified capture.
- Clears the render target, then emits one D3D12 clear rectangle per replay draw up to `--d3d12-draws`.
- Compiles a small HLSL VS/PS pair with `D3DCompile`, builds a root signature and graphics PSO, and submits one `DrawInstanced(6, 1, 0, 0)` synthetic rectangle per replay draw.
- Colors each rectangle from VS hash, PS hash, primitive type, and source select.
- Copies the render target to a readback heap and writes a top-down 32-bit BMP.

This is intentionally diagnostic. It is native GPU output from BO2 replay data, but not BO2 scene rendering.

Current `d3d12` real replay behavior:

- Parses and reconstructs replay state.
- Refuses to synthesize geometry or shaders.
- Fails with an explicit blocker if the capture lacks real index-buffer bytes, vertex fetch constants, vertex-buffer bytes, translated input layouts, and replacement BO2 shaders.

## Translation rules

Draw packets:

- `PM4_DRAW_INDX` (`opcode 0x22`, observed count `60`): indexed draw. Use captured `index_base`, `index_length`, `index_format`, `index_endianness`, and `index_count`.
- `PM4_DRAW_INDX_2` (`opcode 0x36`, observed count `4328`): auto-index or immediate draw path. For `source_select=2`, synthesize a sequential index/vertex stream until real vertex fetch capture is available.
- Primitive type `4` is the clearest PC `triCount * 3` match in the current capture; primitive types `1` and `8` need Xenos topology mapping before GPU submission.

Shaders:

- Runtime capture gives stable 64-bit hashes and raw guest/host microcode ranges.
- Native backend should not execute Xenos microcode directly. It needs a replacement table keyed by stage plus hash.
- First replacement candidates from replay:
  - VS `0xB6C9863F710683EC` with PS `0xA4A965C189287B99`, `4008` draws.
  - VS `0x1E6883FCCDE1F688` with PS `0xA4A965C189287B99`, `235` draws.
  - VS `0x81311AC4B1FBD082` with PS `0x246E20EF10E0DDC7`, `50` draws.
- D3D12 PSOs should be created lazily from replacement shaders plus current blend/depth/raster/topology state.

Constants:

- `PM4_SET_SHADER_CONSTANTS` and `PM4_LOAD_ALU_CONSTANT` are captured as `pm4_constants`.
- Replay tracks total constants, recent constants, and current bound constant ranges per draw. It currently flags `1209` early draws before any constant upload has been captured.
- The BO2 capture/replay structs now support optional bounded constant payload dwords and report missing/truncated state. The existing 20,000-event capture has `170` constants and `0` payload-bearing constants because it predates the ReXGlue-side payload fields.

Render targets and textures:

- Present snapshots identify `PM4_XE_SWAP` frontbuffer and dimensions.
- A real backend needs color/depth target binds, texture fetch constants, sampler state, surface formats, tiling/swizzle, and memory residency.
- Add CP trace events for render-target setup packets, texture/fetch constant loads, and surface state before attempting real BO2 scene output.

Synchronization:

- The current function map lists `PM4_EVENT_WRITE`, `PM4_WAIT_REG_MEM`, `PM4_WAIT_FOR_IDLE`, `PM4_MEM_WRITE`, and visibility-query candidates.
- D3D12 replay can ignore most sync packets for the first triangle-equivalent validation, but runtime backend needs fences/resource barriers for correctness.

## First visible native output target

Completed first steps:

- `native_render_replay.exe --backend d3d12-diagnostic --d3d12-output ... --d3d12-draws 4096 --no-summary` exits `0`.
- It writes a nonblank BMP using native D3D12 command submission and readback.
- The latest D3D12 replay output includes actual graphics-pipeline draw calls with runtime-compiled debug shaders.

Next visible D3D12 target should still be deliberately small:

1. Add a window/swapchain path next to the current offscreen BMP path.
2. Replace synthetic `SV_VertexID` rectangles with real replay-derived index/vertex buffers where state exists.
3. Add replacement shaders for top replay pairs and draw them through PSOs.
4. Present without using ReXGlue/Xenia final rendering.

This proves the BO2-owned backend path, but it is still a diagnostic renderer until vertex fetch, render targets, textures, and real shader replacements are connected.

## Blockers before real BO2 rendering

- Current D3D12 replay output uses synthetic debug shaders and rectangles, not BO2 vertex/index buffers or real BO2 replacement shaders.
- Captured draw state lacks vertex fetch buffers and texture/sampler bindings.
- Existing captured constants only have address/count metadata, not payload values. New capture files can carry payload once the corresponding ReXGlue CP trace callback changes are compiled into the runtime.
- Shader replacement table is not connected to runtime hashes.
- Real D3D12 replay is blocked at draw `1209`: the draw has real index metadata and two bound constant ranges, but no raw index bytes, no decoded vertex/fetch state, no vertex bytes, and no replacement shaders.
- Frame boundaries are present-driven; most current PM4 work is pre-frame in replay terms.
- Android/ARM64 direct generated calls can still bypass dispatcher hooks outside the CP sink.
- ReXGlue SDK callback changes are required for fresh constant payload captures and live resource snapshots. The BO2 hook path compiles with or without the new fields, but payloads remain `missing` until the SDK side is rebuilt.

## Next implementation steps

1. Extend `NativeRendererPM4*` trace callbacks to include bounded constant payload and vertex/index/fetch state snapshots.
2. Add `ReplayRenderState` as a stable normalized state object between JSONL replay and real backends.
3. Add a D3D12 replay swapchain/window path.
4. Add a shader replacement registry keyed by `(stage, hash)` with minimal passthrough/debug shaders for the top replay pairs.
5. Add replay-derived vertex/index buffers once fetch-state capture is extended.
6. Move runtime backend selection from `NullDebug` only to `NullDebug` / `D3D12Debug` once replay D3D12 geometry is backed by real captured state.
