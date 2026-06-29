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
- `native_render_replay.exe` now builds as a standalone Windows console target from the `default` CMake project. It parses JSONL captures, reconstructs per-draw shader/constant state, validates captures, dumps frame/draw state, reports shader usage, reports missing real resource snapshots, and can run an offscreen D3D12 diagnostic backend.
- `native_render_replay.exe --backend d3d12-diagnostic` produced BO2-owned native GPU-output artifacts from replayed draw events: first clear tiles, then a shader-pipeline synthetic geometry pass.
- `native_render_replay.exe --backend d3d12` now produces a BO2-owned native GPU-output artifact from captured draw `1004` using real replayed vertex/index payloads, D3D12 upload buffers, and `DrawIndexedInstanced`. This is captured BO2 geometry with a diagnostic native shader, not shader-correct scene rendering.
- `native_shader_inspect.exe` has been added as a standalone shader index inspection target. The current implementation loads `shader_work/shaders/index.json`, prints summary counts, previews first pixel/vertex containers, and searches static container/microcode records by hash substring.
- `native_shader_inspect.exe` now reads native captures, ranks runtime-used shader hashes and shader pairs, computes captured PM4 shader-payload SHA-256 variants, and checks whether runtime IDs or payload hashes appear directly in the static shader index. `shader_payload_capture_001` has `8` unique runtime shaders, `7` pairs, and `0/8` matches across runtime IDs, raw payload LE/BE hashes, and trailing-zero-trimmed payload LE/BE hashes.
- Backend bring-up plan now lives in `docs/native_renderer_backend_plan.md`; replay format and verified results live in `docs/native_renderer_replay.md`.
- Runtime renderer selection is controlled by the ReXGlue cvar `native_renderer_mode`.

## Renderer modes

- `emulated`: default. No native renderer hooks are installed and the current ReXGlue renderer path is preserved.
- `native`: installs native renderer logging hooks around Xbox video present calls, forwards to ReXGlue `VdSwap`, and uses the null/debug backend as a temporary command sink.
- `native_null` or `null`: installs the same hooks but suppresses forwarded `VdSwap`, so it is useful only for debugging native present interception.

Planned mode names are `native_capture`, `native_d3d12_diagnostic`, `native_d3d12`, `native_vulkan_diagnostic`, and `native_vulkan`. They are not wired into live runtime selection yet; only replay-side backend names exist for the current D3D12 diagnostic path.

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
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --draw 1209 --dump-bound-state --dump-constants --dump-indices --dump-vertices --resource-summary --no-summary
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12-diagnostic --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-updated.bmp --d3d12-draws 4096 --no-summary
default\out\build\win-amd64-clangmsvc-debug\native_shader_inspect.exe --index shader_work\shaders\index.json --summary
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
- `src/native_renderer/replay/NativeShaderInspect.cpp`
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
- Size: `78921728` bytes.
- Last write time: `2026-06-28 22:32:01`.

Runtime verification:

- `native-renderer-structured-run.log`: 45-second `native` run, intentionally force-stopped, `1595821` bytes. It registered the ReXGlue CP trace sink, installed 11 generated-function detours with `patch_size=14`, installed 2 host import detours, and logged live `CP shader`, `CP draw`, `CP constants`, and `CP swap` events.
- `native-renderer-capture-limit.jsonl`: 20-second bounded capture, intentionally force-stopped after the capture hit its own limit. It contains exactly `20000` complete JSONL events: `8601` `pm4_packet`, `1011` `pm4_shader`, `4388` `pm4_draw`, `170` `pm4_constants`, `85` `pm4_swap`, `5399` `render_command`, plus `vd_swap`, `present_snapshot`, and frame markers.
- `native-renderer-final-smoke.jsonl`: final rebuilt binary smoke run with `native_renderer_capture_limit=100`. It contains exactly `100` complete JSONL events and the log confirms `BO2 native renderer capture reached event limit 100`.
- All Windows runs had empty redirected stdout/stderr. `ExitCode=-1` is expected for these verification runs because the process was intentionally stopped after capture/log collection.
- `native_render_replay.exe`: built successfully from the Windows `default` build. Latest verified size `951808` bytes, last write `2026-06-28 22:33:38`.
- Replay summary on `native-renderer-capture-limit.jsonl`: `20000` events, `0` parse errors, `86` frames, `4388` draws, `8` unique live shader hashes, `7` shader pairs, `0` missing VS/PS draws, and `1209` draws before any captured constants.
- Updated replay summary reports existing capture constant payload coverage as `with_payload=0`, `missing_payload=170`, `payload_dwords=0`.
- Draw `1209` is the first useful indexed draw after constants in the verified capture: `PM4_DRAW_INDX`, `index_base=0x0501E0A0`, `index_len=12`, `index_format=0`, `endian=1`, VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`, and two bound ALU constant ranges. The old capture lacks raw constant/index/vertex bytes for it.
- Replay validation on `native-renderer-final-smoke.jsonl`: `Validation OK: 100 events, 2 frames, 24 draws`.
- D3D12 replay output on `native-renderer-capture-limit.jsonl`: `native-renderer-d3d12-replay.bmp`, exit code `0`, size `3686454` bytes, SHA-256 `D82EED84E69EED945B8DA0FF70D7F97B80FCCB35392D5E1E822809231DA03E37`. The BMP was visually checked and is nonblank.
- D3D12 geometry replay output on `native-renderer-capture-limit.jsonl`: `native-renderer-d3d12-geometry-replay.bmp`, exit code `0`, size `3686454` bytes, SHA-256 `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`. The BMP was visually checked and shows shader-pipeline draw tiles over the clear-tile layer.
- Updated D3D12 diagnostic replay output on `native-renderer-capture-limit.jsonl`: `native-renderer-d3d12-diagnostic-updated.bmp`, exit code `0`, size `3686454` bytes, SHA-256 `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`.
- Fresh payload capture `C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\events.jsonl` was taken from the rebuilt `default.exe` with `native_renderer_capture_limit=25000`. It contains `25000` complete JSONL events, `5471` PM4 draws, `227` constant uploads with payload, and `81` indexed draw snapshots with raw index bytes. Validation result: `Validation OK: 25000 events, 106 frames, 5471 draws`.
- New first indexed target in that capture is draw `1209`: `PM4_DRAW_INDX`, `index_base=0x0501E090`, `index_len=12`, `index_format=0`, `endian=1`, VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`, two constant ranges with payload, and raw index bytes `00 03 00 00 00 02 00 02 00 00 00 01`. Replay decodes them as indices `3,0,2,2,0,1`.
- D3D12 diagnostic replay on `payload_capture_002` succeeded and wrote `C:\Users\braxt\bo2-recompiled\native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp`, size `3686454` bytes, SHA-256 `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`.
- Fresh vertex-fetch capture `C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl` was taken from the rebuilt `default.exe` with `native_renderer_capture_limit=12000`. It contains `12000` complete JSONL events, `2646` PM4 draws, `84` constant uploads with payload, `31` indexed draw snapshots, `222` vertex fetch records, and `222` bounded vertex-buffer snapshots containing `16892` raw vertex bytes. Validation result: `Validation OK: 12000 events, 53 frames, 2646 draws`.
- First indexed target with both index and vertex bytes is draw `1004`: `PM4_DRAW_INDX`, `index_base=0x050082B0`, `index_len=12`, `index_format=0`, `endian=1`, VS `0x5D918D91043B3ED0`, PS `0xC4ED2979F29C9139`, two constant ranges with payload, decoded indices `3,0,2,2,0,1`, and `vf95` at `0x05008230` with stride `32`, Xenos formats `38`, `6`, `37`, and `128/128` vertex bytes.
- D3D12 diagnostic replay on `vertex_fetch_capture_001` succeeded and wrote `C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\native-renderer-d3d12-vertex-fetch-diagnostic.bmp`, size `3686454` bytes, SHA-256 `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`.
- Real `--backend d3d12 --draw 1004` now succeeds for the first supported captured draw and wrote `C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp`, size `3686454` bytes, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`. Pixel validation found `921600/921600` non-clear pixels at `1280x720`. This path still uses a diagnostic shader and lacks texture/sampler, render-target/depth/blend/raster state, and replacement/translated BO2 shaders.
- `--backend vulkan-diagnostic` and `--backend vulkan` fail closed because no Vulkan backend exists in this tree yet.
- `native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --summary` succeeds: `142` zones, `36769` occurrences, `33950` unique containers, `33355` unique microcode blobs, `0` invalid bounds.
- `native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --list-runtime-shaders --top-shaders 20` succeeds: `12000` lines, `594` shader events, `2646` draw events, `8` unique runtime shaders, `7` shader pairs.
- `native_shader_inspect.exe --index C:\Users\braxt\bo2-recompiled\shader_work\shaders\index.json --capture C:\Users\braxt\bo2-recompiled\native_captures\vertex_fetch_capture_001\events.jsonl --match-runtime-shaders --top-shaders 20` succeeds and reports `Runtime shader direct matches: 0/8`.
- Full shader-payload rebuild completed with `default`, `native_render_replay`, and `native_shader_inspect` linked successfully. Build log: `default\out\build\win-amd64-clangmsvc-debug\native-renderer-shader-payload-build.out.log`; exit file: `native-renderer-shader-payload-build.exit.txt` = `0`.
- Fresh shader-payload capture `C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl` contains `12000` JSONL events, `53` frames, `2654` draws, `587/587` shader uploads with payload (`13230` dwords), `73/73` constant uploads with payload, `28` indexed draw snapshots, and `218` vertex fetch snapshots. Validation result: `Validation OK: 12000 events, 53 frames, 2654 draws`.
- Target draw `1209` in `shader_payload_capture_001` is a real indexed `PM4_DRAW_INDX` quad with index bytes `00 03 00 00 00 02 00 02 00 00 00 01`, decoded indices `3,0,2,2,0,1`, `vf95` at `0x0501E030`, stride `32`, Xenos formats `38`, `6`, `37`, and decoded `1280x720` position/color/UV vertices.
- Runtime shader direct static-index matches remain `0/8` on the shader-payload capture. `native_shader_inspect.exe` now computes raw little-endian, raw big-endian, trailing-zero-trimmed little-endian, and trailing-zero-trimmed big-endian SHA-256 values for each captured runtime shader payload. All eight runtime shaders have stable payload hashes across repeated uploads, but all four payload-hash match rules report `no` against `shader_work/shaders/index.json`.
- `--backend d3d12` now fails closed unless a real translated/cached/override shader is available. The temporary resource-backed diagnostic shader path requires explicit `--allow-diagnostic-shader`; with that flag, draw `1209` writes `native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`.
- A first manual D3D12 override pair for draw `1209` is present under `shader_work\native_overrides`: `vs_5D918D91043B3ED0.hlsl` and `ps_C4ED2979F29C9139.hlsl`, with a documenting `overrides.json`. `--backend d3d12 --draw 1209` now compiles those overrides, binds flattened captured constants at `b1`, and writes `native-renderer-d3d12-real-draw1209-constant-bound.bmp`, SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`. This is still one real-resource draw with manual interface-compatible shaders, not automatic Xenos shader translation or full scene rendering.
- The override manifest is now parsed by replay, and compiled D3D12 override blobs are cached under `shader_work\cache`: `shader_cache_index.json`, two `.dxbc` blobs, and two compile logs for draw `1209`.
- Cache-only strict replay was verified with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, output SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`.
- Auto real D3D12 replay without `--draw` now batches fully supported draws for the first supported shader pair. On `shader_payload_capture_001`, it submits `11/11` draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139` and writes `native-renderer-d3d12-real-supported-pair-multidraw-log.bmp`, SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`.
- The strict missing-shader path was rechecked with empty override and empty cache roots: exit code `1`, with the expected missing `VS=0x5D918D91043B3ED0` and `PS=0xC4ED2979F29C9139` message.
- The earlier 180-second full-build timeout was superseded by the shader-payload rebuild above. The current known build state is successful for `default`, `native_render_replay`, and `native_shader_inspect`.
- Shader-record probe rebuild completed successfully after Ninja recovered a damaged build log. Build exit file `default\out\build\win-amd64-clangmsvc-debug\native-renderer-shader-probe-rebuild.exit.txt` is `0`. Future iterations should prefer narrow `-j8` Ninja targets because this recovery pass rebuilt the large `default` target with `-j1`.
- Fresh shader-record capture `C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_003\events.jsonl` contains `4514` events, `22` frames, `1026` draws, `190/190` shader uploads with payload, `66` vertex fetch snapshots, and `2` `shader_record_probe` events. Validation result: `Validation OK: 4514 events, 22 frames, 1026 draws`.
- `--shader-record-probes` now decodes printable big-endian ASCII runs from captured dword snapshots and normalizes shader names starting at `pimp_shader_`. The two verified probe names are `pimp_shader_cinematic_519f564_ps_main_ps_3_0_534c8cc25dea1826410cc2974d7e9a80.updb` and `pimp_shader_radiant_190f4788_vs_main_vs_3_0_e10bcefc8da60302d0bbf12b675d091c.updb`.
- Direct lookup of `534c8cc25dea1826410cc2974d7e9a80` and `e10bcefc8da60302d0bbf12b675d091c` in `shader_work\shaders\index.json` / `index.csv` returned no matches. This proves the captured XEX shader-record names are useful runtime identity metadata, but they are not direct keys into the current extracted shader-work index.
- `native_shader_inspect.exe` now parses `shader_record_probe` events, prints stage guesses plus normalized probe names, hashes secondary pointer payloads in little-endian, big-endian, and trailing-zero-trimmed forms, and includes probe-name/probe-payload matching in `--match-runtime-shaders`.
- Probe-aware matching on `shader_probe_capture_003` reports `Runtime shader direct matches: 0/4` and `Shader record probe matches: names=0/2 suffixes=0/2 secondary_payloads=0/2`.
- Shader-record probes are now opt-in through `native_renderer_shader_record_probe_mode=on`; the default `off` path was verified because longer normal captures were crashing while writing probe snapshots.
- Fresh state capture `C:\Users\braxt\bo2-recompiled\native_captures\state_capture_004\events.jsonl` was taken from the rebuilt `default.exe` with `native_renderer_shader_record_probe_mode=off` and `native_renderer_capture_limit=12000`. It contains `12000` JSONL events, `48` frames, `2475` PM4 draws, `691/691` shader uploads with payload, `136/136` constant uploads with payload, `30` index-buffer snapshots, `251` vertex-fetch snapshots, `344` texture-fetch snapshots, and render state on all `2475` draws. Validation result: `Validation OK: 12000 events, 48 frames, 2475 draws`.
- `state_capture_004` has `0` `shader_record_probe` events, proving the normal capture path no longer hits the probe snapshot crash. The process was explicitly stopped after the file reached the bounded event limit.
- First textured indexed target in `state_capture_004` is draw `799`: `PM4_DRAW_INDX`, packet `0xC0032201`, `index_base=0x04FF24E0`, `index_len=12`, decoded indices `3,0,2,2,0,1`, `vf95` at `0x04FF2460`, stride `32`, four texture fetch records, two constant ranges with payload, and render state with `surface_pitch=1280`, `depth_base=608`, `depth_format=1`, color base `1328`, color mask `0x0000000F`, depth test/write disabled, and cull mode `2`.
- Replay resource summary on `state_capture_004` reports texture fetch coverage as `draws_with=60`, `records=344`, `texture_snapshots=344`, `payload_bytes=1409024`, `missing=0`, `truncated=0`; render-state coverage is `draws_with=2475`, `missing=0`.
- D3D12 diagnostic replay on `state_capture_004` succeeded and wrote `C:\Users\braxt\bo2-recompiled\native-renderer-state-capture-004-d3d12-diagnostic.bmp`.
- D3D12 real replay on `state_capture_004` succeeded for the existing supported manual-override shader pair and submitted `4/4` captured draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139`, writing `C:\Users\braxt\bo2-recompiled\native-renderer-state-capture-004-d3d12-real.bmp`. This is still not full scene rendering; at that point the backend had not yet bound captured textures or render state.
- D3D12 real replay now binds the first decodable captured texture fetch per supported draw through a shader-visible SRV at `t0`. On `state_capture_004`, it reports `4` captured texture SRVs, `0` fallback SRVs, and `0` unsupported texture attempts, writing `C:\Users\braxt\bo2-recompiled\native-renderer-state-capture-004-d3d12-texture-bound.bmp` with SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- D3D12 real replay now uses a shader-visible sampler descriptor table at `s0` and writes one sampler descriptor per uploaded draw from captured texture-filter fields. On older `state_capture_004`, it reports `4` captured sampler descriptors, `0` fallback sampler descriptors, and writes `C:\Users\braxt\bo2-recompiled\native-renderer-state-capture-004-d3d12-sampler-bound.bmp` with SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`; because that capture predates serialized clamp modes, replay uses clamp-addressing fallback.
- Fresh clamp capture `C:\Users\braxt\bo2-recompiled\native_captures\clamp_capture_001\events.jsonl` validates `12000` events, `47` frames, and `2448` draws. Replay resource summary reports `344/344` texture fetch records with clamp modes. D3D12 real replay on that capture submits `4/4` supported draws and reports `exact_clamp_modes=4, clamp_addressing_fallbacks=0`, writing `native-renderer-clamp-capture-001-d3d12-sampler-clamp.bmp` with SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Old-capture regression on `state_capture_004` still works: D3D12 real replay reports `exact_clamp_modes=0, clamp_addressing_fallbacks=4` and writes `native-renderer-state-capture-004-d3d12-sampler-clamp-fallback.bmp` with SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- D3D12 real replay now applies a first captured render-state subset to the real replay PSO. On `state_capture_004`, it reports `D3D12 real replay applied render state from draw 799: color_mask=0x0000000F cull=2 depth_test=no depth_write=no stencil=no`, writing `native-renderer-state-capture-004-d3d12-render-state.bmp` with SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- The same D3D12 real path still works on older captures without texture fields by binding fallback white textures. On `shader_payload_capture_001`, it reports `0` captured texture SRVs and `11` fallback SRVs, writing `native-renderer-shader-payload-d3d12-texture-binding-regression.bmp` with SHA-256 `49F99D11F992073E0DF9371E37EE57DC0522032336E44ADAAFC00CEDE12E3D2A`.

## What works

- The project builds with native renderer code linked into both app targets.
- `native_renderer_mode=emulated` keeps the existing renderer path untouched.
- `native_renderer_mode=native` and `native_renderer_mode=native_null` initialize a backend-neutral renderer facade and install hooks for the Xbox present imports.
- The null/debug backend receives frame begin, `VdSwap`, `PM4_XE_SWAP` present-packet, and frame end events.
- The null/debug backend can receive sampled draw-candidate logs with command-buffer object, write range, packet offset, index count, primitive type, source select, and index-size flag when execution reaches the installed dispatcher entries or a successfully installed generated-function trampoline.
- ReXGlue CP execution now submits structured `PM4_DRAW_INDX`, `PM4_DRAW_INDX_2`, shader-load, constant-upload, and swap events to the backend-neutral renderer.
- Parsed draw packets are submitted to the backend as `RenderCommandType::DrawIndexed`; shader loads are submitted as `RenderCommandType::BindShader`.
- JSONL capture can record the live render stream for replay/backend bring-up.
- Offline replay reconstructs draw state enough to list draw opcode, packet pointer, index/primitive/source fields, bound VS/PS hashes, constant history, bound constant ranges, and missing-state flags per draw.
- Offline replay now parses optional constant payload arrays and reports whether each constant upload has payload, is missing payload, or is truncated.
- Offline replay now parses optional shader payload arrays and reports whether each PM4 shader upload has payload, is missing payload, or is truncated.
- Offline replay now parses `shader_record_probe` events and can print register state, command-buffer write range, primary/secondary dword snapshots, ASCII strings, and normalized `pimp_shader_*` runtime names with `--shader-record-probes`.
- Offline replay now parses bounded raw index-byte payloads for indexed draws, validates indexed draws with missing snapshots, and decodes 16-bit/32-bit indices using the captured Xenos endian mode.
- Offline replay now has explicit `--dump-indices`, `--dump-vertices`, and `--resource-summary` reports. Index dumps can show real decoded BO2 indices for fresh captures; vertex/fetch dumps can show real fetch constants, stream base/size/stride, shader-decoded attribute formats, and bounded raw vertex bytes.
- Offline replay now decodes captured vertex payload bytes for the observed Xenos fetch formats. Draw `1004` decodes to a real `1280x720` indexed quad with position/color/UV attributes, and draw `24` decodes non-indexed position data from format `57`.
- Offline replay now parses texture fetch records and decoded render-state records from fresh captures. `--dump-bound-state` prints texture base/mip addresses, dimensions, format, tiling/endian/filter summary, payload byte count, and the current surface/depth/color/raster basics.
- Offline D3D12 replay creates a native D3D12 render target, emits draw-derived clear rectangles, compiles a tiny HLSL VS/PS pair, submits synthetic triangle draw calls, copies the target to CPU memory, and writes a BMP without using ReXGlue/Xenia final rendering.
- Offline real D3D12 replay can bind decoded captured BO2 vertex/index data for draw `1004`/`1209`, bind captured format-6 texture fetches for the current supported shader pair, submit `DrawIndexedInstanced`, copy the target to CPU memory, and write a BMP. Draw `1209`/`799` can use a hash-keyed manual HLSL override pair; the temporary diagnostic shader still exists only behind `--allow-diagnostic-shader`.
- Ghidra MCP is usable for both programs: `CoDMPServer_PC.exe` provides PDB-backed renderer symbols, and `default.xex` instruction searches verify the XEX packet emitter addresses even where Ghidra's PPC function boundaries are broken.

## What does not work yet

- There is not yet a complete real Vulkan/D3D12/Metal/deko3d backend.
- There is a D3D12 debug replay backend that renders diagnostic draw tiles and synthetic shader-pipeline rectangles.
- There is a first D3D12 resource-backed replay path that submits all captured BO2 indexed draws for one supported shader pair with real vertex/index buffers, flattened captured constants, captured format-6 texture SRVs when present, captured texture-filter and clamp-mode sampler descriptors, a first captured PSO-side render-state subset, and a manual HLSL override pair, but it does not yet translate Xenos shaders, reconstruct full constant layouts, decode all texture formats/tiling, bind real depth targets, or replay full render-target/depth state.
- Runtime draw calls, render target changes, shader bindings, texture bindings, and buffer uploads are not translated into a real in-game backend yet.
- The selected draw-candidate hook and CP trace sink submit backend-neutral commands, but the null backend only logs/captures them and no separate BO2-owned GPU backend consumes them yet.
- Old captures have constant/index metadata but no raw index bytes. Fresh captures after ReXGlue commit `3ed291c` plus the index payload trace update carry bounded constant payload dwords and bounded indexed-draw byte snapshots.
- Replay currently has draw/index metadata, constant payloads, bounded index snapshots, vertex fetch records, bounded vertex snapshots, bounded texture fetch snapshots, captured texture-filter/clamp sampler descriptors, decoded render-state fields, and CPU-side vertex component decoding for observed formats. It still lacks sidecar manifests, complete texture decode/upload, render-target/depth resource snapshots, and full backend application of blend/raster/depth state across heterogeneous draws.
- Frame markers are present/swap-derived. Most current PM4 work in the verified capture is pre-frame from replay's perspective, so backend frame grouping must use CP stream plus present packets rather than only current `begin_frame` / `end_frame`.
- Android/ARM64 direct generated calls can still bypass dispatcher hooks. An ARM64-safe generated-function detour or generated-call rewrite is still needed before every logged candidate is guaranteed to fire on Android.
- The XEX equivalents for the static material asset-load functions are not fully mapped. The current map is strongest for runtime packet emitters and shader/material binding.
- The cleanest XEX draw-packet target is now `0x8258CF68` (`PM4_DRAW_INDX_2` with variable initiator).
- Shader replacement now exists for one runtime pair as D3D12 override files loaded through a parsed manifest or deterministic filename fallback. The compiled override pair is cached and can satisfy strict replay without override source. Runtime shader ranking and reproducible payload-hash evidence now exist, but runtime-to-static shader identity is still unproven.

## Next highest-impact targets

1. Add an ARM64-safe generated-call interception path or generated-call rewrite, so Android direct calls cannot bypass dispatcher hooks.
2. Capture material shader record metadata and use Ghidra-backed shader/material records so runtime hashes can be matched to static shader containers or explicit overrides.
3. Replace flattened captured constants with layout-aware constant buffers for draw `1209`.
4. Expand texture decoding beyond 1x1 tiled format `6`, including larger tiled textures, mip footprint handling, and additional runtime-used formats.
5. Add a backend-neutral `ReplayRenderState` layer between JSONL replay and real GPU backends and apply color/depth/blend/raster state in D3D12.
6. Add sidecar resource manifests for larger vertex, texture, and render-target snapshots.
7. Replace the null runtime backend with a `D3D12Debug` backend once the replay D3D12 path proves device/swapchain/indexed draw submission.
