# BO2 shader workflow

ReXGlue already translates Xbox 360 Xenos shaders to the active native graphics
backend and persists the shader and pipeline cache. Native pipeline variants
depend on live draw state, so they cannot be reconstructed from shader
microcode alone. The project tools seed validated guest microcode; ReXGlue then
compiles and stores only the native variants that BO2 actually uses.

## Extract

Decode the Xbox fastfiles and XEX base images into one directory, then run:

```powershell
npm install
npm run shaders:extract -- shader_work/fastfile_dat shader_work/shaders
```

The existing World-disc extraction contains 33,355 unique programs and no
invalid container bounds.

## Validate

Validate every extracted file against its indexed size, stage-qualified SHA-256
hash, instruction alignment, and ReXGlue-compatible XXH3 hash:

```powershell
npm run shaders:cache -- inspect
```

## Native runtime cache

The extracted container set is structurally valid, but it is not safe to place
directly in ReXGlue's live cache. A runtime test reached unsupported Xenos
operations such as export register 48. The cache builder can create research
`.xsh` files with `--output`, but it deliberately cannot install them.

Use the project launcher to build a compatible cache from shaders and exact
pipeline variants encountered during real gameplay:

```powershell
# Campaign
npm run shaders:run -- --app default

# Multiplayer
npm run shaders:run -- --app default_mp

# Zombies
npm run shaders:run -- --app default_mp --mode zombies
```

The caches are stored under `shader_cache/default` and
`shader_cache/default_mp`. Reuse the same launcher on later runs. ReXGlue reads
the `.xsh` shader storage and `.xpso` D3D12 pipeline descriptions at startup,
translates the required native variants in parallel, and creates the cached
pipelines before gameplay.

This reduces repeat-run shader compilation stutter. It does not increase
steady-state frame rate because ReXGlue already runs translated native
DXBC/SPIR-V rather than interpreting Xenos shaders per frame.
