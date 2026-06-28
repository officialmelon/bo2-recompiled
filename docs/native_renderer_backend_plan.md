# Native Renderer Backend Plan

Last updated: 2026-06-28

This plan describes the path from the current capture/replay milestone to a BO2-owned native renderer. It does not claim that native rendering is complete. The current executable backend is still null/offline replay analysis.

## Current state

- Runtime hooks and the ReXGlue command-processor trace sink capture PM4 packet, shader, constant, draw, swap, and present events.
- `native_render_replay.exe` parses verified captures and reconstructs draw state with bound VS/PS hashes and recent constant uploads.
- `native_render_replay.exe --backend d3d12` now creates a BO2-owned D3D12 device, renders an offscreen debug target from replayed draw events, reads it back, and writes a BMP.
- The first verified replay run parsed `20000` events, `4388` draws, `1011` shader loads, `170` constant uploads, `85` PM4 swaps, and `86` present snapshots with `0` parse errors.
- The first verified D3D12 replay output is `native-renderer-d3d12-replay.bmp`, size `3686454`, SHA-256 `D82EED84E69EED945B8DA0FF70D7F97B80FCCB35392D5E1E822809231DA03E37`.
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

The replay tool now reconstructs items 1, 2, 3, 6, 7, and 8 enough to print state. Items 4 and 5 need additional capture fields before a real backend can draw BO2 geometry correctly.

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

Current implemented D3D12 replay behavior:

- Creates a D3D12 device, direct queue, command allocator/list, render target, RTV heap, readback buffer, and fence.
- Chooses output dimensions from the first present/swap event, currently `1280x720` for the verified capture.
- Clears the render target, then emits one D3D12 clear rectangle per replay draw up to `--d3d12-draws`.
- Colors each rectangle from VS hash, PS hash, primitive type, and source select.
- Copies the render target to a readback heap and writes a top-down 32-bit BMP.

This is intentionally diagnostic. It is native GPU output from BO2 replay data, but not BO2 scene rendering.

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
- Replay tracks total constants and recent constants per draw. It currently flags `1209` early draws before any constant upload has been captured.
- Backend needs constant payload bytes, not only address/count. Extend the CP trace sink to copy a bounded constant payload into JSONL for replay.

Render targets and textures:

- Present snapshots identify `PM4_XE_SWAP` frontbuffer and dimensions.
- A real backend needs color/depth target binds, texture fetch constants, sampler state, surface formats, tiling/swizzle, and memory residency.
- Add CP trace events for render-target setup packets, texture/fetch constant loads, and surface state before attempting real BO2 scene output.

Synchronization:

- The current function map lists `PM4_EVENT_WRITE`, `PM4_WAIT_REG_MEM`, `PM4_WAIT_FOR_IDLE`, `PM4_MEM_WRITE`, and visibility-query candidates.
- D3D12 replay can ignore most sync packets for the first triangle-equivalent validation, but runtime backend needs fences/resource barriers for correctness.

## First visible native output target

Completed first step:

- `native_render_replay.exe --backend d3d12 --d3d12-output ... --d3d12-draws 4096 --no-summary` exits `0`.
- It writes a nonblank BMP using native D3D12 command submission and readback.

Next visible D3D12 target should still be deliberately small:

1. Add a window/swapchain path next to the current offscreen BMP path.
2. Draw debug geometry for replayed draw calls using synthetic vertex data and replacement shaders.
3. Color-code draw calls by shader pair and primitive type.
4. Present without using ReXGlue/Xenia final rendering.

This proves the BO2-owned backend path, but it is still a diagnostic renderer until vertex fetch, render targets, textures, and real shader replacements are connected.

## Blockers before real BO2 rendering

- Current D3D12 replay output uses clear rectangles, not vertex/pixel shaders or indexed geometry.
- Captured draw state lacks vertex fetch buffers and texture/sampler bindings.
- Captured constants only have address/count metadata, not payload values.
- Shader replacement table is not connected to runtime hashes.
- Frame boundaries are present-driven; most current PM4 work is pre-frame in replay terms.
- Android/ARM64 direct generated calls can still bypass dispatcher hooks outside the CP sink.
- ReXGlue SDK callback changes are in the external `C:\Users\braxt\rexglue-sdk` worktree and need their own clean checkpoint before this repo is reproducible from a fresh clone.

## Next implementation steps

1. Extend `NativeRendererPM4*` trace callbacks to include bounded constant payload and vertex/index/fetch state snapshots.
2. Add `ReplayRenderState` as a stable normalized state object between JSONL replay and real backends.
3. Add a D3D12 replay swapchain/window path and synthetic debug geometry per replay draw.
4. Add a shader replacement registry keyed by `(stage, hash)` with minimal passthrough/debug shaders for the top replay pairs.
5. Move runtime backend selection from `NullDebug` only to `NullDebug` / `D3D12Debug` once replay D3D12 geometry is proven.
