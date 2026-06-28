# Native Renderer Progress

Last updated: 2026-06-28

## Current status

- Work branch: `codex/native-renderer-wip`.
- Existing source/config/script work was checkpointed first in commit `87f8cb2`.
- Native renderer scaffolding now lives under `src/native_renderer/`.
- `default` and `default_mp` both build the scaffold and install renderer hooks from their app-specific source trees.
- The native renderer now snapshots the ReXGlue-written present command buffer after `VdSwap` and logs the `PM4_XE_SWAP` packet through the backend interface.
- Generated-code packet scans identified medium-confidence `PM4_DRAW_INDX_2` and `PM4_IM_LOAD_IMMEDIATE` XEX hook candidates in `docs/native_renderer_function_map.md`.
- A Ghidra MCP pass expanded the SP `default.xex` graphics map with concrete packet emitters for draw, shader microcode load, shader constants, indirect buffers, events, waits, and present/swap. The current source of truth is `docs/native_renderer_function_map.md`.
- Draw-candidate loggers are installed for SP `default` packet emitters `0x825828D8`, `0x82582A30`, `0x8258A5E8`, and `0x8258CF68`, plus MP packet emitters `0x82117BC8`, `0x82117D20`, `0x8212E280`, and `0x8212EB40`. They snapshot command-buffer writes, parse `PM4_DRAW_INDX_2` payloads, translate them into backend-neutral `DrawIndexed` commands, and then call the original PPC function.
- Shader/material packet loggers are installed for SP `0x8258CCE8`, `0x8258CE40`, `0x82597DF8`, `0x82597F50`, `0x82598140`, and MP `0x8212D478`; they log caller LR, r3-r6, write pointer, write limit, and return value.
- `default` also logs command-buffer grow/reserve helpers at `0x8257AB00` and `0x8257AD38`. These hooks provide guest-address context for render packet emitters and command-buffer allocation/submission behavior.
- Windows hosts install generated-function detours for these hooks after saving the dispatcher originals, so direct generated calls are captured there. Android/ARM64 currently keeps dispatcher replacement only.
- ReXGlue's command processor now exposes a structured native-renderer trace sink for render-relevant PM4 packets. The BO2 native renderer receives live packet, draw, shader, constant, and swap events from that sink.
- A backend-neutral JSONL capture writer is available through `native_renderer_capture_path`; it records replayable frame, present, PM4, shader, constant, draw, and backend-neutral command events.
- Windows `default.exe` was rebuilt and run in `native` mode on 2026-06-28. The run logs prove the CP trace sink, generated-function detours, shader binds, draw packets, constants, swaps, and capture writer all fire.
- `native_render_replay.exe` now builds as a standalone Windows console target from the `default` CMake project. It parses JSONL captures, reconstructs per-draw shader/constant state, validates captures, dumps frame/draw state, reports shader usage, and can run an offscreen D3D12 debug backend.
- `native_render_replay.exe --backend d3d12` produced the first BO2-owned native GPU-output artifact from replayed draw events: `native-renderer-d3d12-replay.bmp`.
- Backend bring-up plan now lives in `docs/native_renderer_backend_plan.md`; replay format and verified results live in `docs/native_renderer_replay.md`.
- Runtime renderer selection is controlled by the ReXGlue cvar `native_renderer_mode`.

## Renderer modes

- `emulated`: default. No native renderer hooks are installed and the current ReXGlue renderer path is preserved.
- `native`: installs native renderer logging hooks around Xbox video present calls, forwards to ReXGlue `VdSwap`, and uses the null/debug backend as a temporary command sink.
- `native_null` or `null`: installs the same hooks but suppresses forwarded `VdSwap`, so it is useful only for debugging native present interception.

`native_renderer_verbose` controls high-frequency logging. The logger prints the first few frame events and then periodic samples.

## Capture controls

- `native_renderer_capture_path`: JSONL file path. Empty disables capture.
- `native_renderer_capture_limit`: maximum captured events. `0` means unlimited.
- `native_renderer_capture_flush_interval`: captured events between file flushes. The file also flushes immediately when the event limit is reached.

The capture format is line-delimited JSON. Important event types are `pm4_packet`, `pm4_shader`, `pm4_draw`, `pm4_constants`, `pm4_swap`, `render_command`, `vd_swap`, `present_snapshot`, `begin_frame`, and `end_frame`.

Offline replay:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --summary --shader-usage --top-shaders 20
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --frame 2 --dump-draws --max-draws 24
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12 --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-replay.bmp --d3d12-draws 4096 --no-summary
```

## Files changed for native renderer

- `src/native_renderer/NativeRenderer.h/.cpp`
- `src/native_renderer/NativeRenderCaptureWriter.h/.cpp`
- `src/native_renderer/RendererBackend.h`
- `src/native_renderer/RendererTypes.h`
- `src/native_renderer/RenderCommand.h`
- `src/native_renderer/RenderResourceManager.h/.cpp`
- `src/native_renderer/ShaderManager.h/.cpp`
- `src/native_renderer/TextureManager.h/.cpp`
- `src/native_renderer/BufferManager.h/.cpp`
- `src/native_renderer/DebugRenderLog.h/.cpp`
- `src/native_renderer/HostDetour.h/.cpp`
- `src/native_renderer/backend_null/NullRendererBackend.h/.cpp`
- `src/native_renderer/replay/NativeRenderReplay.h/.cpp`
- `src/native_renderer/replay/D3D12ReplayBackend.cpp`
- `src/native_renderer/replay/NativeRenderReplayTool.cpp`
- `src/native_renderer/shader_translation/ShaderTranslation.h/.cpp`
- `default/src/native_renderer_hooks.cpp`
- `default_mp/src/native_renderer_hooks.cpp`
- `default/CMakeLists.txt`
- `default_mp/CMakeLists.txt`
- `default/src/default_app.h`
- `default_mp/src/default_mp_app.h`
- ReXGlue SDK side: `C:\Users\braxt\rexglue-sdk\include\rex\graphics\command_processor.h` and `C:\Users\braxt\rexglue-sdk\src\graphics\command_processor.cpp` expose and fire the CP trace callbacks.

## Intercepted path

The first real rendering path intercepted is the Xbox video present path:

- `default`: import `__imp__VdSwap` at `0x826EAF9C`, command-buffer GPU identifier import at `0x826EAF4C`.
- `default_mp`: import `__imp__VdSwap` at `0x827FBB94`, command-buffer GPU identifier import at `0x827FBA14`.

The hook records the `VdSwap` arguments, submits them to the backend-neutral renderer, forwards to ReXGlue unless `native_renderer_mode=native_null`, then snapshots the first 16 dwords of the 64-dword present command buffer that ReXGlue writes. The null backend detects `PM4_XE_SWAP`, logs the packet dword offset, physical frontbuffer address, and presented dimensions.
The host import detour is currently implemented for Windows hosts only. Android builds still compile this path, but direct generated import calls continue through the ReXGlue SDK until an Android-safe host detour or lower-level generated-call interception is added.

## Build and run verification

Android builds completed earlier:

```powershell
cmake --build --preset android-arm64-debug --target default -j 4
cmake --build --preset android-arm64-debug --target default_mp -j 4
```

Windows `default` was built by entering the VS developer environment manually and invoking Ninja:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j1 default"
```

Final verified executable:

- `C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\default.exe`
- Size: `78917120` bytes.
- Last write time: `2026-06-28 14:56:32`.

Runtime verification:

- `native-renderer-structured-run.log`: 45-second `native` run, intentionally force-stopped, `1595821` bytes. It registered the ReXGlue CP trace sink, installed 11 generated-function detours with `patch_size=14`, installed 2 host import detours, and logged live `CP shader`, `CP draw`, `CP constants`, and `CP swap` events.
- `native-renderer-capture-limit.jsonl`: 20-second bounded capture, intentionally force-stopped after the capture hit its own limit. It contains exactly `20000` complete JSONL events: `8601` `pm4_packet`, `1011` `pm4_shader`, `4388` `pm4_draw`, `170` `pm4_constants`, `85` `pm4_swap`, `5399` `render_command`, plus `vd_swap`, `present_snapshot`, and frame markers.
- `native-renderer-final-smoke.jsonl`: final rebuilt binary smoke run with `native_renderer_capture_limit=100`. It contains exactly `100` complete JSONL events and the log confirms `BO2 native renderer capture reached event limit 100`.
- All Windows runs had empty redirected stdout/stderr. `ExitCode=-1` is expected for these verification runs because the process was intentionally stopped after capture/log collection.
- `native_render_replay.exe`: built successfully from the Windows `default` build. Verified size `888320` bytes, last write `2026-06-28 16:44:28`.
- Replay summary on `native-renderer-capture-limit.jsonl`: `20000` events, `0` parse errors, `86` frames, `4388` draws, `8` unique live shader hashes, `7` shader pairs, `0` missing VS/PS draws, and `1209` draws before any captured constants.
- Replay validation on `native-renderer-final-smoke.jsonl`: `Validation OK: 100 events, 2 frames, 24 draws`.
- D3D12 replay output on `native-renderer-capture-limit.jsonl`: `native-renderer-d3d12-replay.bmp`, exit code `0`, size `3686454` bytes, SHA-256 `D82EED84E69EED945B8DA0FF70D7F97B80FCCB35392D5E1E822809231DA03E37`. The BMP was visually checked and is nonblank.

## What works

- The project builds with native renderer code linked into both app targets.
- `native_renderer_mode=emulated` keeps the existing renderer path untouched.
- `native_renderer_mode=native` and `native_renderer_mode=native_null` initialize a backend-neutral renderer facade and install hooks for the Xbox present imports.
- The null/debug backend receives frame begin, `VdSwap`, `PM4_XE_SWAP` present-packet, and frame end events.
- The null/debug backend can receive sampled draw-candidate logs with command-buffer object, write range, packet offset, index count, primitive type, source select, and index-size flag when execution reaches the installed dispatcher entries or a successfully installed generated-function trampoline.
- ReXGlue CP execution now submits structured `PM4_DRAW_INDX`, `PM4_DRAW_INDX_2`, shader-load, constant-upload, and swap events to the backend-neutral renderer.
- Parsed draw packets are submitted to the backend as `RenderCommandType::DrawIndexed`; shader loads are submitted as `RenderCommandType::BindShader`.
- JSONL capture can record the live render stream for replay/backend bring-up.
- Offline replay reconstructs draw state enough to list draw opcode, packet pointer, index/primitive/source fields, bound VS/PS hashes, constant history, and missing-state flags per draw.
- Offline D3D12 replay creates a native D3D12 render target, emits draw-derived clear rectangles, copies the target to CPU memory, and writes a BMP without using ReXGlue/Xenia final rendering.
- Ghidra MCP is usable for both programs: `CoDMPServer_PC.exe` provides PDB-backed renderer symbols, and `default.xex` instruction searches verify the XEX packet emitter addresses even where Ghidra's PPC function boundaries are broken.

## What does not work yet

- There is not yet a real Vulkan/D3D12/Metal/deko3d backend.
- There is a D3D12 debug replay backend, but it renders diagnostic draw tiles with clear calls. It does not yet submit BO2 indexed geometry, shader replacements, textures, or render-target/depth state.
- Runtime draw calls, render target changes, shader bindings, texture bindings, and buffer uploads are not translated into a real in-game backend yet.
- The selected draw-candidate hook and CP trace sink submit backend-neutral commands, but the null backend only logs/captures them and no separate BO2-owned GPU backend consumes them yet.
- Replay currently has constant upload metadata but not constant payload bytes. A real backend needs payload capture.
- Replay currently has draw/index metadata but not complete vertex fetch, texture fetch, sampler, render-target, or depth/stencil state.
- Frame markers are present/swap-derived. Most current PM4 work in the verified capture is pre-frame from replay's perspective, so backend frame grouping must use CP stream plus present packets rather than only current `begin_frame` / `end_frame`.
- Android/ARM64 direct generated calls can still bypass dispatcher hooks. An ARM64-safe generated-function detour or generated-call rewrite is still needed before every logged candidate is guaranteed to fire on Android.
- The XEX equivalents for the static material asset-load functions are not fully mapped. The current map is strongest for runtime packet emitters and shader/material binding.
- The cleanest XEX draw-packet target is now `0x8258CF68` (`PM4_DRAW_INDX_2` with variable initiator).
- Shader replacement exists only as a documented pipeline direction; no native shader override table is bound into the renderer yet.

## Next highest-impact targets

1. Add an ARM64-safe generated-call interception path or generated-call rewrite, so Android direct calls cannot bypass dispatcher hooks.
2. Extend the CP trace sink/capture writer with constant payload bytes, vertex fetch state, texture fetch state, render target binds, and depth/stencil state.
3. Add a backend-neutral `ReplayRenderState` layer between JSONL replay and real GPU backends.
4. Extend the D3D12 replay backend from clear-rectangle output to window/swapchain output and synthetic indexed geometry.
5. Connect runtime shader/microcode hashes from `shader_work/shaders/index.json` to a replacement shader registry keyed by `(stage, hash)`.
6. Replace the null runtime backend with a `D3D12Debug` backend once the replay D3D12 path proves device/swapchain/indexed draw submission.
