# Native Renderer D3D12 Status

Last updated: 2026-07-01

## Diagnostic backend

`--backend d3d12-diagnostic` works in offline replay.

Verified target-built command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture default\out\build\win-amd64-clangmsvc-debug\native-renderer-capture-limit.jsonl --backend d3d12-diagnostic --d3d12-output default\out\build\win-amd64-clangmsvc-debug\native-renderer-d3d12-diagnostic-target.bmp --d3d12-draws 4096 --no-summary
```

Result:

- Exit code: `0`
- Output: `native-renderer-d3d12-diagnostic-target.bmp`
- Size: `3686454`
- SHA-256: `08D49F9F79AC9B77C1A895B110C77D563FC821448D3E65BCA34EC72752333C39`

Latest index-payload capture command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\payload_capture_002\events.jsonl --backend d3d12-diagnostic --d3d12-output native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp --d3d12-draws 512 --no-summary
```

Result:

- Exit code: `0`
- Output: `native_captures\payload_capture_002\native-renderer-d3d12-index-payload-diagnostic.bmp`
- Size: `3686454`
- SHA-256: `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`

Latest vertex-fetch capture command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\vertex_fetch_capture_001\events.jsonl --backend d3d12-diagnostic --d3d12-output native_captures\vertex_fetch_capture_001\native-renderer-d3d12-vertex-fetch-diagnostic.bmp --d3d12-draws 512 --no-summary
```

Result:

- Exit code: `0`
- Output: `native_captures\vertex_fetch_capture_001\native-renderer-d3d12-vertex-fetch-diagnostic.bmp`
- Size: `3686454`
- SHA-256: `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`

Latest shader-payload capture diagnostic command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\shader_payload_capture_001\events.jsonl --backend d3d12-diagnostic --d3d12-output native_captures\shader_payload_capture_001\native-renderer-d3d12-diagnostic-shader-payload.bmp --d3d12-draws 512 --no-summary
```

Result:

- Exit code: `0`
- Output: `native_captures\shader_payload_capture_001\native-renderer-d3d12-diagnostic-shader-payload.bmp`
- SHA-256: `6A3E73E74CC22538420B2FCDA6B5B7D72A4EA205EAAAD11476ED700554F6C577`

## Real backend

`--backend d3d12` now has a first offline resource-backed geometry path. It selects a captured indexed draw with real index and vertex snapshots, canonicalizes the decoded Xenos vertex data into a native D3D12 input layout, binds upload-buffer vertex/index resources, compiles a hash-keyed manual HLSL override pair when present, and submits `DrawIndexedInstanced`.

This is still not full BO2 scene rendering. Draws using the `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139` pair can now run without `--allow-diagnostic-shader` because a manual HLSL override pair exists under `shader_work\native_overrides`, and the pair can be reused through `shader_work\cache\shader_cache_index.json`. The override is interface-compatible with the currently canonicalized position/color/UV vertex stream and consumes a flattened captured constant payload block at `b1`; on `state_capture_004` it also samples captured format-6 texture SRVs through dynamic sampler descriptors and uses the first captured PSO-side render-state subset. It is not an automatic Xenos shader translation. Without a matching cache entry, override, translation, or explicit diagnostic fallback, `--backend d3d12` fails closed.

Current missing pieces:

- Automatic Xenos shader translation and persistent DXIL shader cache
- Complete captured constant-buffer layout binding beyond the current flattened `b1` root constants
- Complete texture tiling/format coverage and full sampler LOD/mip validation
- Render target/depth resources and full heterogeneous blend/raster/depth state
- Multi-draw/full-frame state sequencing for all captured draw types

Current captured pieces from `native_captures\vertex_fetch_capture_001`:

- Constant payloads: `84/84`
- Indexed draw snapshots: `31/31`, `372` raw index bytes total
- Vertex fetch records: `222`, all with bounded raw vertex payloads, `16892` raw vertex bytes total
- First indexed draw `1004`: decoded indices `3,0,2,2,0,1`, `vf95`, stride `32`, attributes `(format=38, offset=0)`, `(format=6, offset=16)`, `(format=37, offset=20)`, `128/128` vertex bytes, and decoded position/color/UV vertices for a `1280x720` quad
- Replay-side vertex decoding covers the observed formats `57`, `38`, `37`, and `6`, plus the common packed/integer/half/float variants implemented in `native_render_replay`
- Real D3D12 output for draw `1004`: `native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-draw1004-final.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`, `1280x720`, `921600/921600` non-clear pixels
- Auto-selected real D3D12 output: `native_captures\vertex_fetch_capture_001\native-renderer-d3d12-real-auto-final.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Diagnostic D3D12 after the real path remains working: `native-renderer-d3d12-diagnostic-final.bmp`, SHA-256 `D9FA1C81D99553D78089CFEC9987DA2D1D6ABB051351A456C4CAF0731A9450E3`
- Fresh shader-payload capture `native_captures\shader_payload_capture_001`: `587/587` shader uploads with payload, `73/73` constant uploads with payload, `28` indexed draw snapshots, `218` vertex fetch snapshots
- Draw `1209` with explicit diagnostic shader fallback: `native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp`, SHA-256 `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`
- Draw `1209` with manual overrides `vs_5D918D91043B3ED0.hlsl` and `ps_C4ED2979F29C9139.hlsl`: `native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-constant-bound.bmp`, SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Draw `1209` with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, cache-only output SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Auto real replay without `--draw` now batches every fully supported draw using the first supported shader pair. For `shader_payload_capture_001`, it submits `11/11` captured draws for `VS=0x5D918D91043B3ED0` / `PS=0xC4ED2979F29C9139` and writes `native-renderer-d3d12-real-supported-pair-multidraw-log.bmp`, SHA-256 `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Draw `1209` with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root native_captures\empty_shader_cache` and no `--allow-diagnostic-shader`: exit code `1`, expected fail-closed message with the exact missing VS/PS hashes

Latest state-capture replay results:

- Capture: `native_captures\state_capture_004\events.jsonl`
- Validation: `Validation OK: 12000 events, 48 frames, 2475 draws`
- New replay data available to the backend: `344` texture fetch snapshots with `1409024` payload bytes and render state on all `2475` draws.
- Combined target draw: `799`, with real index bytes, `vf95` vertex bytes, four texture fetch records, two constant ranges, and render state.
- Diagnostic command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12-diagnostic --d3d12-output native-renderer-state-capture-004-d3d12-diagnostic.bmp --d3d12-draws 512 --no-summary`
- Diagnostic result: exit code `0`, output `native-renderer-state-capture-004-d3d12-diagnostic.bmp`
- Real command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-real.bmp --d3d12-draws 256 --no-summary`
- Real result: exit code `0`, `D3D12 real replay submitted 4 supported draw(s) for shader pair VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 out of 4 captured draw(s) with that pair`.
- Limitation for that pre-texture-binding run: the output remained the existing manual-override supported geometry path and was not full BO2 scene rendering.
- Texture-binding update: D3D12 real replay now creates shader-visible SRV descriptors for decodable captured texture fetches and binds captured sampler descriptors from texture-filter/clamp fields. The current implementation supports captured Xenos formats `2` (`k_8`), `6` (`k_8_8_8_8`), `18` (`DXT1`), `19` (`DXT2_3`), `20` (`DXT4_5`), `26` (`16_16_16_16`), `38` (`32_32_32_32_FLOAT`), and `49` (`DXN`) as RGBA8 upload/preview sources. Unsupported texture-bearing draws still fail closed in strict real mode unless an explicit diagnostic-shader fallback is requested.
- Texture-bound command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-texture-bound.bmp --d3d12-draws 256 --no-summary`
- Texture-bound result: exit code `0`, `D3D12 real replay bound 4 captured texture SRV(s), 0 fallback texture SRV(s), unsupported_texture_attempts=0`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Cache-only texture-bound result with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, `4` captured texture SRVs bound, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Sampler-binding update: D3D12 real replay now uses a shader-visible sampler descriptor table at `s0` and creates one sampler descriptor per uploaded draw from captured texture-filter fields. Fresh captures also serialize Xenos clamp modes and map them to D3D12 address modes; old captures without those fields keep the previous clamp-addressing fallback.
- Sampler-bound command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-sampler-bound.bmp --d3d12-draws 256 --no-summary`
- Sampler-bound result: exit code `0`, `D3D12 real replay bound 4 captured sampler descriptor(s), 0 fallback sampler descriptor(s)`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Cache-only sampler-bound result with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, `4` captured texture SRVs and `4` captured sampler descriptors bound, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Clamp capture: `native_captures\clamp_capture_001\events.jsonl`
- Clamp capture validation: `Validation OK: 12000 events, 47 frames, 2448 draws`; resource summary reports `texture_fetch_records=344 ... clamp_modes=344/344 missing_clamp_modes=0`.
- Clamp-mapped D3D12 command: `native_render_replay.exe --capture native_captures\clamp_capture_001\events.jsonl --backend d3d12 --d3d12-output native-renderer-clamp-capture-001-d3d12-sampler-clamp.bmp --d3d12-draws 256 --no-summary`
- Clamp-mapped D3D12 result: exit code `0`, `D3D12 real replay bound 4 captured sampler descriptor(s), 0 fallback sampler descriptor(s), exact_clamp_modes=4, clamp_addressing_fallbacks=0`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Tiled format-6 replay command: `native_render_replay.exe --capture native_captures\clamp_capture_001\events.jsonl --backend d3d12 --d3d12-output native-renderer-clamp-capture-001-d3d12-tiled-format6.bmp --d3d12-draws 256 --no-summary`
- Tiled format-6 replay result: exit code `0`, `4` captured texture SRVs, `0` fallback texture SRVs, `exact_clamp_modes=4`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Old-capture clamp fallback result on `state_capture_004`: exit code `0`, `exact_clamp_modes=0, clamp_addressing_fallbacks=4`, output `native-renderer-state-capture-004-d3d12-sampler-clamp-fallback.bmp`, SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Render-state update: D3D12 real replay now builds the real replay PSO from the first supported draw's captured render state for rasterizer culling/front-face, fill mode, depth-clip, color write mask, blend factors/ops, and disabled depth/stencil state. It also applies captured screen scissor when a bounded scissor rectangle is present.
- Render-state command: `native_render_replay.exe --capture native_captures\state_capture_004\events.jsonl --backend d3d12 --d3d12-output native-renderer-state-capture-004-d3d12-render-state.bmp --d3d12-draws 256 --no-summary`
- Render-state result: exit code `0`, `D3D12 real replay applied render state from draw 799: color_mask=0x0000000F cull=2 depth_test=no depth_write=no stencil=no`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Cache-only render-state result with `--shader-override-root native_captures\empty_shader_overrides --shader-cache-root shader_work\cache`: exit code `0`, same applied-state log and SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Sidecar texture-payload capture: `native_captures\sidecar_capture_002\events.jsonl`
- Sidecar validation: `Validation OK: 12000 events, 52 frames, 2628 draws`; resource summary reports `texture_fetch_records=412`, `texture_snapshots=412`, `sidecars=372`, `sidecar_bytes=3999744`, `payload_bytes=4004864`, `truncated=186`, and `sidecar_resource_manifest=present json=yes jsonl=yes`.
- Sidecar real command: `native_render_replay.exe --capture native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output native-renderer-sidecar-capture-002-d3d12-real.bmp --d3d12-draws 256 --no-summary`
- Sidecar real result: exit code `0`, `D3D12 real replay submitted 5 supported draw(s) for shader pair VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 out of 5 captured draw(s) with that pair`, `5` captured texture SRVs, `0` fallback texture SRVs, `5` captured sampler descriptors, `exact_clamp_modes=5`, applied render state from draw `748`, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Sidecar diagnostic command: `native_render_replay.exe --capture native_captures\sidecar_capture_002\events.jsonl --backend d3d12-diagnostic --d3d12-output native-renderer-sidecar-capture-002-d3d12-diagnostic.bmp --no-summary`
- Sidecar diagnostic result: exit code `0`, output SHA-256 `538733ED1A0DEF3B4BA0FD0CA2F600C61742ACEDBE6E761BEACA62116F4E9327`.
- Frontbuffer sidecar capture: `native_captures\frontbuffer_capture_001\events.jsonl`
- Frontbuffer validation: `Validation OK: 3000 events, 16 frames, 673 draws`; resource summary reports `frontbuffer_snapshots=15`, `payload_bytes=55296000`, `sidecars=15`, `sidecar_bytes=55296000`, and `truncated=0`.
- Frontbuffer content status: the first observed `1280x720x4` sidecars are all zero, so the D3D12 backend cannot use PM4 swap payloads as scene color yet. The next D3D12 resource blocker is draw-time color/depth target snapshots from captured render-state target bases.
- Frontbuffer regression command on `sidecar_capture_002`: `native_render_replay.exe --capture native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output native-renderer-frontbuffer-regression-d3d12-real.bmp --d3d12-draws 256 --no-summary`
- Frontbuffer regression result: exit code `0`, `5` captured texture SRVs, `5` exact-clamp sampler descriptors, output SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Target snapshot capture: `native_captures\target_snapshot_capture_003\events.jsonl`
- Target snapshot validation: `Validation OK: 1800 events, 10 frames, 395 draws`; resource summary reports `color_target_snapshots=368`, `color_target_payload_bytes=6029312`, `depth_target_snapshots=369`, `depth_target_payload_bytes=6045696`, all sidecar-backed and truncated to bounded 16 KiB previews.
- Target snapshot draw evidence: draw `29` reports color target base `1328`, depth base `608`, `payload=16384/3686400`, `offset=1843200`, sidecar-backed and truncated.
- Target snapshot blocker: scanning all color/depth target sidecars in the capture found only zero bytes. The D3D12 backend still needs a GPU-side ReXGlue/Xenos render-target readback or resolve path; CPU memory copies from the target base addresses are not enough.
- Target snapshot D3D12 regression: `native_render_replay.exe --capture native_captures\sidecar_capture_002\events.jsonl --backend d3d12 --d3d12-output native-renderer-target-snapshot-regression-d3d12-real.bmp --d3d12-draws 256 --no-summary` exits `0` with unchanged SHA-256 `6C10E0294634A70F7B465C8DF951875A65717920F8320498E139933DE4E34421`.
- Texture recapture update: BO2-side texture payload storage is now dynamic, and truncated texture fetches for known formats `2` and `6` are recaptured from guest physical memory with an 8 MiB per-fetch cap before JSONL sidecar writing. Fresh bounded capture `native_captures\texture_recapture_001\events.jsonl` reports `344` texture fetch records, `216582656` sidecar bytes, and `truncated=0`. D3D12 replay of draw `909` for `VS=0x81311AC4B1FBD082` / `PS=0x246E20EF10E0DDC7` binds `4` captured SRVs and `4` captured samplers with `partial_texture_previews=0`, applies draw `909` render state, and writes `native-renderer-texture-recapture-draw909-v6-d3d12-real.bmp` with SHA-256 `5745996D338C74D4C481CA87EBA0FCA6D6A66FBA6AF190AFB3DFDDC8AB32E9EA`. The output is still not final scene rendering because the v6 PS rule is a partial resource-dependent lowering rather than complete predicated Xenos ALU translation.
- Vertex recapture update: BO2-side vertex payload storage is now dynamic, and truncated vertex fetches are recaptured from guest physical memory with an 8 MiB per-fetch cap. Fresh bounded capture `native_captures\vertex_recapture_001\events.jsonl` reports `260` vertex fetch records, `432792` vertex payload bytes, and `truncated=0`, compared with `64` truncated vertex snapshots in `texture_recapture_001`.
- Point-list real replay update: `xenos_limited_semantic_v7` compiles generated DXIL for `VS=0x5B9B7484417FB9B6` / `PS=0x3A6876055FEC1674`. Explicit D3D12 replay of `vertex_recapture_001` draw `1966` submits `1` supported non-indexed point-list draw out of `65` for the pair, expands captured `FMT_16_16_16_16` point payload records to small quads for bring-up visibility, applies captured render state (`color_mask=0x0000000F`, `cull=0`, depth/stencil disabled), and writes `native-renderer-vertex-recapture-draw1966-renderstate-d3d12-real.bmp`. This proves another BO2-payload-derived D3D12 path but is not full scene rendering; the full Xenos `eA/eM` point/particle export behavior is still not decoded.
- Top-pair non-indexed replay update: `xenos_limited_semantic_v7` also compiles generated DXIL for `VS=0x1E6883FCCDE1F688` paired with top PS `0xA4A965C189287B99`. Explicit D3D12 replay of `vertex_recapture_001` draw `24` submits `1` supported draw out of `120` for the pair from a real `84/84` byte vf0 payload and applies captured render state with depth/stencil enabled. The output is black because the captured interpolator exported to `r0` is zero and the PS is an exact `max oC0, r0, r0` export, not because the replay fell back to synthetic geometry.
- Explicit batch replay update: selected-draw real replay now honors `--d3d12-draws` for later compatible draws with the same shader pair. On `vertex_recapture_001`, `--draw 24 --d3d12-draws 128` submits `120/120` supported real draws for `VS=0x1E6883FCCDE1F688` / `PS=0xA4A965C189287B99`; `--draw 1966 --d3d12-draws 80` submits `65/65` supported real draws for `VS=0x5B9B7484417FB9B6` / `PS=0x3A6876055FEC1674`. The batch still uses one PSO/render-state snapshot from the first draw, so per-draw PSO switching is still required before mixed-state full-frame rendering is correct.
- Frame replay planning update: when a capture has frame markers but no parser-owned frame draws, D3D12 real replay now derives per-frame draw ranges from event sequence numbers instead of assigning every pre-frame PM4 draw to frame `0`. Frame `0` uses `(0, frame0_end_seq]`; later frames use `(previous_frame_boundary_seq, selected_frame_boundary_seq]`. The old all-pre-frame bucket remains only as a last-resort frame-0 fallback if no sequence range can be inferred.
- Sequence frame regression: `native_render_replay.exe --capture native_captures\vertex_recapture_001\events.jsonl --frame 0 --backend d3d12 --shader-override-root empty_shader_overrides --shader-cache-root shader_work\cache --d3d12-output native-renderer-seq-frame0-strict-d3d12.bmp --no-summary` exits `0`, reports `frame_draws=24`, uses sequence bucket `seq=(0,93]`, and submits `24` real BO2-derived no-fetch draws for `VS=0xB6C9863F710683EC` / `PS=0xA4A965C189287B99`.
- Later sequence frame regression: `native_render_replay.exe --capture native_captures\vertex_recapture_001\events.jsonl --frame 3 --backend d3d12 --skip-unsupported --shader-override-root empty_shader_overrides --shader-cache-root shader_work\cache --d3d12-output native-renderer-seq-frame3-skip-d3d12.bmp --no-summary` exits `0`, uses sequence bucket `seq=(173,305]`, submits `26` real draws across `3` shader pairs, and reports `depth_enabled_draws=1 depth_write_draws=1 stencil_enabled_draws=1`.
- Multi-pair frame replay update: D3D12 frame skip replay builds a small PSO cache keyed by runtime VS/PS pair, preserves prepared draw order, switches root signature/PSO per draw, and submits every supported draw whose pipeline can be created. Earlier all-pre-frame bring-up submitted all `260/260` geometry-supported real BO2-derived draws across `7` shader pairs. With sequence-inferred buckets enabled, the same PSO switching is applied to each selected frame range; frame `3` currently submits `26` draws across `3` shader pairs from `seq=(173,305]`.
- Render-state PSO-key update: the D3D12 real replay PSO cache key now includes runtime VS/PS hashes, expanded primitive topology, render target format, captured depth/color formats, blend/depth/stencil/raster registers, decoded depth/stencil/cull/fill/front-face flags, color mask, and MSAA sample count. Same-pair draws are split by captured state where required.
- Phase 1 input/resource update: the D3D12 real replay path now carries a dynamic input-layout mask from decoded vertex fetches into the PSO key and input layout. The canonical `POSITION/COLOR0/TEXCOORD0` elements remain available for current translated shaders, while captured layouts can add `NORMAL0` and `TEXCOORD1` variants. The vertex decoder now assigns positions per vertex instead of only for the first decoded vertex. Texture decode now accepts additional uncompressed Xenos formats `26` (`16_16_16_16`, converted to RGBA8 preview data) and `38` (`32_32_32_32_FLOAT`, clamped to RGBA8 preview data) in addition to formats `2` and `6`.
- Block-compressed texture update: the D3D12 replay texture decoder now handles captured block-compressed formats `18` (`DXT1`), `19` (`DXT2_3`), `20` (`DXT4_5`), and `49` (`DXN`) as RGBA8 upload/preview data. On `native_captures\swap_fetch_capture_001\events.jsonl`, strict `--backend d3d12` still fails closed at the missing shader pair `VS=0x162EAA53D8B42911 PS=0x6973911F04C7B340`; with explicit `--allow-diagnostic-shader` for resource validation, D3D12 replay submits `7/7` draws for that pair and binds `28` captured texture SRVs and `28` captured sampler descriptors with `unsupported_texture_attempts=0` and `0` fallbacks.
- Phase 1 D3D12 validation: `native_render_replay.exe --capture native_captures\vertex_recapture_001\events.jsonl --draw 24 --d3d12-draws 128 --backend d3d12 --shader-override-root empty_shader_overrides --shader-cache-root shader_work\cache --d3d12-output native-renderer-phase1-draw24-d3d12.bmp --no-summary` exits `0`, submits `120/120` draws for `1E6883/A4A965`, and reports `PSO cache: entries=3 misses=3 hits=117`. The same capture with `--frame 0 --backend d3d12 --skip-unsupported` exits `0`, submits `260` draws across `7` shader pairs, and reports `PSO cache: entries=10 misses=10 hits=250`, `240` captured texture SRVs, and output `native-renderer-pair-frame0-d3d12.bmp`. `d3d12-diagnostic` remains separate and exits `0`.
- Pair-specific shader-interface update: PS `0xC4ED2979F29C9139` and PS `0x246E20EF10E0DDC7` are now selected per VS interface. Indexed VS paths (`5D918D/C4ED` and `81311A/246E`) keep the earlier color+UV PS variants; `AB1E86/C4ED` and `AB1E86/246E` use UV-only v7 PS variants because `AB1E86` exports its interpolator through `TEXCOORD0` and does not export `COLOR0`. Direct replays of draw `698` and draw `910` now exit `0`, bind captured texture/sampler state, and submit the previously failing AB1E texture pairs.
- Real texture strictness update: real D3D12 replay now checks captured texture fetches before upload. If a draw references a captured texture that cannot be decoded, strict real mode fails and frame `--skip-unsupported` skips/counts it; only absent texture fetches keep inert descriptors for root-table completeness. Verified texture-bearing command `--draw 909 --d3d12-draws 32 --backend d3d12` exits `0`, submits `26/26` draws for `81311A/246E`, binds `104` captured texture SRVs and `104` captured samplers, and uses `0` texture fallbacks.
- DSV/depth-state update: the D3D12 real replay path now creates an offscreen `D24_UNORM_S8_UINT` depth/stencil resource, creates a DSV heap/view, declares `DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT` on real replay PSOs, binds the DSV for real draws, and clears depth plus stencil before drawing. Captured `depth_test_enable`, `depth_write_enable`, `depth_func`, stencil enable, stencil masks, and stencil ref now feed the real PSO/stencil state.
- Stencil-op update: D3D12 real replay now decodes Xenos `RB_DEPTHCONTROL` stencil fail, depth-fail, and pass operations for front and back faces into `D3D12_DEPTH_STENCILOP_DESC`. If `backface_enable` is clear, the back-face D3D12 state mirrors the front-face state. The existing DSV path, stencil ref, read mask, and write mask remain unchanged.
- DSV/depth regression command: `native_render_replay.exe --capture native_captures\vertex_recapture_001\events.jsonl --draw 24 --d3d12-draws 128 --backend d3d12 --shader-override-root empty_shader_overrides --shader-cache-root shader_work\cache --d3d12-output native-renderer-depth-draw24-d3d12.bmp --no-summary`
- DSV/depth regression result: exit `0`, `120/120` submitted for `1E6883/A4A965`, `PSO cache: entries=3 misses=3 hits=117`, `depth target format=D24_UNORM_S8_UINT depth_enabled_draws=82 depth_write_draws=82 stencil_enabled_draws=120`.
- DSV frame regression result: `--frame 0 --backend d3d12 --skip-unsupported` exits `0`, submits `260` supported draws across `7` shader pairs, binds `240` captured texture SRVs and `240` captured samplers, and reports `depth_enabled_draws=82 depth_write_draws=82 stencil_enabled_draws=120`.
- DSV texture-pair regression result: `--draw 909 --d3d12-draws 32 --backend d3d12` exits `0`, submits `26/26` draws for `81311A/246E`, binds `104` captured texture SRVs and `104` captured samplers, and reports no depth/stencil-enabled submitted draws for that render-state subset.
- Vertexless point-draw update: the D3D12 real replay path now supports only the proven no-fetch point-list class `VS=0xB6C9863F710683EC` / `PS=0xA4A965C189287B99`, non-indexed `PM4_DRAW_INDX_2`, primitive type `1`, and no captured vertex/fetch records. It binds no vertex buffer, uses `SV_VertexID`, and keeps all other no-fetch classes fail-closed.
- Vertexless direct regression: `native_render_replay.exe --capture native_captures\vertex_recapture_001\events.jsonl --draw 0 --backend d3d12 --shader-override-root empty_shader_overrides --shader-cache-root shader_work\cache --d3d12-output native-renderer-vertexless-draw0-d3d12.bmp --no-summary` exits `0`, submits `2090/2090` draws for `B6C986/A4A965`, creates `3` PSOs, and uses no captured or fallback sampler descriptors because the pair has no texture fetches.
- Vertexless frame regression: sequence-inferred frame `0` now exits `0`, reports `frame_draws=24`, and submits the first `24` supported `B6C986/A4A965` no-fetch point draws from `seq=(0,93]`. The larger all-pre-frame stress run is superseded by selected-draw replay for the full pair: `--draw 0 --backend d3d12` still submits `2090/2090` draws for `B6C986/A4A965`.
- Vertexless strict regression: with sequence-inferred frame buckets, `--frame 0 --backend d3d12` no longer reaches the later unsupported `DDED7E/3A68` no-fetch point shader because that shader is outside frame `0`'s sequence range. The unsupported class remains fail-closed when explicitly selected or reached in later frame ranges: `DDED7E no-fetch point shader needs Xenos r0/register initialization semantics before D3D12 can draw it`.
- Current frame-mode limitation: the D3D12 frame path now switches between multiple shader pairs, captured-state PSO variants, dynamic input-layout variants, and sequence-inferred frame buckets for the current pre-frame capture shape, but it still uses a single offscreen RGBA8 render target and a synthetic depth target. The next required expansion is real render/depth target descriptors, captured depth contents, richer input/resource format mapping, and live capture-boundary integration so PM4 execution and presentation are recorded on the same timeline.
- Live mode checkpoint: `native_renderer_mode=native_d3d12` now parses as a distinct strict live D3D12 mode instead of falling back to `emulated`. It constructs the project-local `D3D12LiveRendererBackend`, initializes an ID3D12Device plus direct command queue, suppresses emulated `VdSwap`, records the live event stream through the existing capture writer, and reports a fail-closed live draw-submission gap rather than using diagnostic geometry. This is not live scene rendering yet; the offline D3D12 replay translator still needs to be factored into this live backend.
- Live mode build evidence: targeted object builds for `src\native_renderer\backend_d3d12\D3D12LiveRendererBackend.cpp` and `src\native_renderer\NativeRenderer.cpp` both exit `0`. Full `default` target rebuild still exceeded the guard twice after the edit while Ninja repeatedly reran CMake/glob verification, so this checkpoint is verified at object-compile level plus replay regression, not as a completed linked `default.exe`.
- Regression command on older `shader_payload_capture_001`: `native_render_replay.exe --capture native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --d3d12-output native-renderer-shader-payload-d3d12-texture-binding-regression.bmp --d3d12-draws 64 --no-summary`
- Regression result: exit code `0`, `D3D12 real replay bound 0 captured texture SRV(s), 11 fallback texture SRV(s), unsupported_texture_attempts=0`, output SHA-256 `49F99D11F992073E0DF9371E37EE57DC0522032336E44ADAAFC00CEDE12E3D2A`.
- Remaining limitation: these SRVs and sampler descriptors are now real captured resources/state in the command stream, PM4 swap/frontbuffer payload capture is implemented, and bounded draw-time color/depth target previews are implemented. The tested target previews are all zero, so the backend still lacks a proven GPU-side render-target/depth readback or resolve path, additional texture formats/mips, exact Xenos stencil-op translation, and full blend/depth/stencil coverage.

## Build note

The CMake build graph was regenerated with Visual Studio CMake. `default`, `native_render_replay`, and `native_shader_inspect` build successfully through Ninja under `VsDevCmd`; latest targeted override-path builds:

- `native_render_replay`: exit `0`, `native_render_replay.exe` size `1076736`, last write `2026-06-29 17:21:11`
- `native_shader_inspect`: exit `0`, `native_shader_inspect.exe` size `812544`, last write `2026-06-29 20:42:39`
