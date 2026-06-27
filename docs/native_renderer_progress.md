# Native Renderer Progress

Last updated: 2026-06-28

## Current status

- Work branch: `codex/native-renderer-wip`.
- Existing source/config/script work was checkpointed first in commit `87f8cb2`.
- Native renderer scaffolding now lives under `src/native_renderer/`.
- `default` and `default_mp` both build the scaffold and install renderer hooks from their app-specific source trees.
- The native renderer now snapshots the ReXGlue-written present command buffer after `VdSwap` and logs the `PM4_XE_SWAP` packet through the backend interface.
- Runtime renderer selection is controlled by the ReXGlue cvar `native_renderer_mode`.

## Renderer modes

- `emulated`: default. No native renderer hooks are installed and the current ReXGlue renderer path is preserved.
- `native`: installs native renderer logging hooks around Xbox video present calls, forwards to ReXGlue `VdSwap`, and uses the null/debug backend as a temporary command sink.
- `native_null` or `null`: installs the same hooks but suppresses forwarded `VdSwap`, so it is useful only for debugging native present interception.

`native_renderer_verbose` controls high-frequency logging. The logger prints the first few frame events and then periodic samples.

## Files changed for native renderer

- `src/native_renderer/NativeRenderer.h/.cpp`
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
- `src/native_renderer/shader_translation/ShaderTranslation.h/.cpp`
- `default/src/native_renderer_hooks.cpp`
- `default_mp/src/native_renderer_hooks.cpp`
- `default/CMakeLists.txt`
- `default_mp/CMakeLists.txt`
- `default/src/default_app.h`
- `default_mp/src/default_mp_app.h`

## Intercepted path

The first real rendering path intercepted is the Xbox video present path:

- `default`: import `__imp__VdSwap` at `0x826EAF9C`, command-buffer GPU identifier import at `0x826EAF4C`.
- `default_mp`: import `__imp__VdSwap` at `0x827FBB94`, command-buffer GPU identifier import at `0x827FBA14`.

The hook records the `VdSwap` arguments, submits them to the backend-neutral renderer, forwards to ReXGlue unless `native_renderer_mode=native_null`, then snapshots the first 16 dwords of the 64-dword present command buffer that ReXGlue writes. The null backend detects `PM4_XE_SWAP`, logs the packet dword offset, physical frontbuffer address, and presented dimensions.
The host import detour is currently implemented for Windows hosts only. Android builds still compile this path, but direct generated import calls continue through the ReXGlue SDK until an Android-safe host detour or lower-level generated-call interception is added.

## Build verification

The available CMake presets in this environment are Android-only. These commands completed successfully:

```powershell
cmake --build --preset android-arm64-debug --target default -j 4
cmake --build --preset android-arm64-debug --target default_mp -j 4
```

The Windows presets are present in JSON but disabled by CMake's host-system condition in this shell, so they were not run.

## What works

- The project builds with native renderer code linked into both app targets.
- `native_renderer_mode=emulated` keeps the existing renderer path untouched.
- `native_renderer_mode=native` and `native_renderer_mode=native_null` initialize a backend-neutral renderer facade and install hooks for the Xbox present imports.
- The null/debug backend receives frame begin, `VdSwap`, `PM4_XE_SWAP` present-packet, and frame end events.

## What does not work yet

- There is not yet a real Vulkan/D3D12/Metal/deko3d backend.
- Draw calls, render target changes, shader bindings, texture bindings, and buffer uploads are not translated yet.
- The XEX equivalents for the core material and draw functions are not fully mapped.
- Shader replacement exists only as a documented pipeline direction; no native shader override table is bound into the renderer yet.

## Next highest-impact targets

1. Map the XEX function that corresponds to Windows `R_DrawIndexedPrimitive` and log indexed draw arguments before the GPU packet path.
2. Expand command-buffer snapshotting from the present packet to draw/setup packets, especially `PM4_DRAW_INDX`, `PM4_SET_CONSTANT`, `PM4_SET_SHADER_CONSTANTS`, and `PM4_IM_LOAD`.
3. Map shader/material load functions from Windows `Material_LoadPass*` to XEX asset loading, then connect hashes from `shader_work/shaders/index.json` to runtime material passes.
4. Replace the null backend with the first real backend implementation, probably D3D12 on Windows because ReXGlue already emits D3D12 pipeline cache artifacts.
