# Native Renderer Vulkan Status

Last updated: 2026-06-28

## Status

No Vulkan backend is implemented in this tree yet.

The replay CLI accepts `--backend vulkan-diagnostic` and `--backend vulkan` so scripts can target the planned names, but both fail closed:

```text
Vulkan replay backend unavailable: no Vulkan backend is implemented in this tree yet
```

## Required before Vulkan work

Vulkan should start after D3D12 real replay proves the backend-neutral resource and shader translation layers. Starting Vulkan now would duplicate missing work: index/vertex snapshots, Xenos fetch decode, shader translation/overrides, texture/sampler decode, and render-state translation.

## Planned backend path

- `src/native_renderer/backend_vulkan/`
- Shared backend-neutral replay/resource/shader state with D3D12.
- SPIR-V shader cache under `shader_work/cache/vulkan/`.
- Diagnostic output first, then real replay, then live mode.
