# Native Renderer Real Backend Status

Last updated: 2026-07-02

## Status

Real native rendering is not complete.

## 2026-07-02 MP010 replay checkpoint

Current best offline real D3D12 replay image:

- `C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-ab1e-zero-elide.bmp`

This is recognizable BO2-derived MP menu/background output, not synthetic
diagnostic geometry. The replay submits captured BO2 draw calls through the real
D3D12 backend with diagnostic shader fallback disabled, using captured
vertex/index payloads, decoded texture sidecars, captured render state, manual
or cached native shaders, and D3D12 PSOs. It is still not complete native scene
rendering: the image is grayscale/incorrect, many utility draws are skipped, and
the next visible AB1E glyph-atlas class is blocked on Xenos
interpolator/register semantics.

Verification command:

```powershell
C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-ab1e-zero-elide.bmp --no-summary
```

Latest verified result after the AB1E zero-texture utility classifier:

- `180` real D3D12 draws submitted across `8` shader pairs.
- `diagnostic_pipelines=0`.
- `196` captured texture SRVs were bound.
- `unsupported_texture_attempts=0`.
- `depth_target_bound=yes`.
- `depth_enabled_draws=38`, `depth_write_draws=38`,
  `stencil_enabled_draws=56`.
- `forced_depth_only_zero_color_draws=56`.
- Color output SHA-256:
  `4D11C18AFD29F24DFF62FBA97EB5FC07BD64ABAFF56FD41016EE8BEEFAE25BF6`.

The `VS=0x1E6883FCCDE1F688 / PS=0xA4A965C189287B99` group is now replayed only
as depth/stencil-affecting zero-color work. Its PSO color write mask is forced
to zero so the pass can update depth/stencil without painting placeholder black
triangles into the color target.

Current AB1E finding:

- `VS=0xAB1E86137A0240E8` semantic IR exports only position (`oPos`) from
  captured XY screen vertices.
- `PS=0xEDC17DCC3FFDB040` and `PS=0xFF01D28E1EF3A880` read interpolator
  registers (`r0`/`r1`) for texture/color data that the current AB1E vertex
  translation does not prove.
- The `AB1E/EDC1` draws in MP010 bind a decoded all-zero `1x1` texture with
  blend control `0x010B0706`; with source alpha zero, they preserve the
  destination. These are now classified as ignored zero-texture no-output
  utility draws, not as scene-ready rendering.
- The `AB1E/FF01` draws bind a real `512x1024` glyph atlas. They remain blocked
  until the replay proves a nonzero pixel input. The MP010 draws currently have
  `SQ_PROGRAM_CNTL=0x00010002`, `param_gen=0`, and the AB1E vertex shader writes
  no interpolators. ReXGlue's translator zeroes such pixel GPRs, so FF01's
  `mul oC0, r1.xxxy, r0` has zero source alpha and preserves the destination
  with the captured `SRC_ALPHA/INV_SRC_ALPHA` blend state. These are classified
  as ignored zeroed-interpolator no-output utility draws, not scene rendering.

Next correctness target:

- Decode the remaining `AB1E/A4` no-texture color-export group. Unlike FF01,
  it has blend disabled, so zero output would overwrite color; it cannot be
  treated as no-output without deeper target/register evidence.

## 2026-07-01 MP replay real-output checkpoint

Current best offline real D3D12 replay image:

- `C:\Users\braxt\bo2-recompiled\native-renderer-mp003-full-after-ef95-dc16.bmp`

This image is still not a correct BO2 frame. It does, however, contain visible
BO2-derived output from captured MP replay data: the dark honeycomb/UI
background, corrupted texture content in the upper-left, and additional
white/gray glyph marks near the lower-left. The replay uses captured BO2
indices, decoded vertex/fetch payloads, decoded texture sidecars, captured
render state, and manual D3D12 shader overrides for the currently understood
runtime shader pairs. Diagnostic shader fallback is not used.

Verification command:

```powershell
C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-full-after-ef95-dc16.bmp
```

Result:

- Exit code `0`.
- `130` supported real D3D12 draws submitted across `6` shader pairs.
- `diagnostic_pipelines=0`.
- `205` captured texture SRVs and `205` captured samplers were bound.
- `835` fallback SRVs and `635` fallback samplers were still bound because the
  current root signature exposes a fixed layout8 descriptor table wider than
  some manually covered shaders actually sample.
- Depth/stencil was not enabled for the submitted draws in this capture.
- Output RGB sample stats improved from the previous `91`-draw image:
  `max=(255,255,255)`, sampled nonzero pixels `36303/57600`.

Submitted shader pairs:

| VS | PS | Submitted | Notes |
|---|---|---:|---|
| `0x3C4F6D40D699817B` | `0xEDC17DCC3FFDB040` | 46 | Textured MP logo/UI class; visible. |
| `0x5B9B7484417FB9B6` | `0x3A6876055FEC1674` | 25 | Existing real-resource coverage. |
| `0xCBC9604F48930B36` | `0x7D1EF030F5710BDA` | 20 | Resource-backed but visually very dark; needs real PS ALU/constant lowering. |
| `0x261BDD733FEC1F64` | `0xFF01D28E1EF3A880` | 19 | New glyph/UI coverage; visible in single draw and full replay. |
| `0xCBC9604F48930B36` | `0x8645E8BA65E424B2` | 10 | Five-texture resource-backed path; visually very dark until real PS ALU/constant lowering. |
| `0xEF95534343684F5B` | `0xDC168FB6031AFC41` | 10 | One-texture UI quad path; single draw shows a small colored strip. |

Single-draw proof for the new `261B/FF01` pair:

```powershell
C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --draw 566 --backend d3d12 --d3d12-draws 1 --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache_261b_ff01_test --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-draw566-261b-ff01.bmp
```

- Exit code `0`.
- Submitted one draw with real 36-index buffer, 24 decoded vertices, one
  captured 512x1024 texture, captured render state, and manual D3D12 shaders.
- The image shows visible white glyph segments near the top-right.

Latest gap report after this checkpoint:

```text
draws=1179 geometry_ok=152 shader_ok=149 texture_ok=149 ready=149 scene_candidate_ready=149
top remaining blockers:
  942 x geometry: no captured vertex/fetch state, but render state has no modeled color/depth/stencil side effects
  63 x geometry: A4 pass-through color export has no captured color/texture dependency or exports all zero
  22 x geometry: DDED7E no-fetch point shader needs Xenos r0/register initialization semantics
  10 x shader: no translated/cached/override VS 0xEF95534343684F5B
  3 x shader: no translated/cached/override PS 0x79E1F538A5074A65
```

The same gap report marks `VS=0xAB1E86137A0240E8 /
PS=0xFF01D28E1EF3A880` as `9` additional ready draws because the `FF01` pixel
override now exists. Those draws are not listed in the `120` submitted full
replay summary yet, so the next replay-backend audit should check the
non-indexed/primitive/submission filters for that pair.

The next correctness target is still automatic shader lowering and live native
D3D12 integration, not more diagnostic output. The highest-value offline
targets are `PS=0x79E1F538A5074A65` and the no-fetch `DDED7E/A4`
register/export semantics.

## 2026-07-01 strict live D3D12 path-fix audit

The current strict live image is:

- `native_captures\live_d3d12_strict_pathfix_001\desktop-strict-pathfix.png`

This image is not a BO2 scene. It shows a large dark diagonal primitive in the
owned `BO2 Native D3D12 - default` window. That is still incorrect output, but
it proves a different state from the older diagnostic screenshots: strict
`native_d3d12` is now submitting real D3D12 replay work with diagnostic shader
fallback disabled.

Code change:

- `src\native_renderer\NativeRenderer.cpp` now resolves relative shader cache
  and override roots against the project tree when the game is launched from
  the CMake/Ninja build directory.
- `src\native_renderer\replay\D3D12ReplayBackend.cpp` now resolves relative
  shader-cache manifest paths such as `shader_work/cache/d3d12/...` against the
  project root, not only against the process current directory.

Verification:

```powershell
default.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_strict_pathfix_001\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
```

- RelWithDebInfo `default` rebuild after the path fix: exit `0`.
- RelWithDebInfo `native_render_replay` rebuild after the path fix: exit `0`.
- Live run was watchdog-stopped after evidence capture, not by a crash.
- Screenshot SHA-256:
  `52EE8F311F6B72927EB2258A0C19505F463169EE3D9007BB0A76B52C8DC2A5EC`.
- `live_d3d12_submit` telemetry reaches frame `30` with `submitted_frames=24`
  and `failed_frames=5`. The failures are frames with no currently supported
  complete geometry, not shader/PSO creation failures.
- D3D12 stdout repeatedly reports real submissions with
  `diagnostic_pipelines=0`, for example `submitted 8 supported draw(s) across
  3 shader pair(s)`, `PSO cache: entries=8 misses=0 hits=8
  diagnostic_pipelines=0`, `bound 8 captured texture SRV(s), 24 fallback
  texture SRV(s)`, `bound 8 captured sampler descriptor(s)`, and a bound
  `D24_UNORM_S8_UINT` depth target.

Fresh capture summary:

- `6500` events, `30` frames, `1458` PM4 draws, `310` shader uploads, `34`
  constant uploads.
- `114` draws have vertex/fetch snapshots and are ready for the current D3D12
  real backend.
- `136` texture fetch records have sidecar payloads totaling `66764800` bytes.
- Render-state records exist on all `1458` draws; color/depth target sidecars
  are present but truncated previews (`16384` bytes per target snapshot).
- Gap report: `draws=1458 geometry_ok=114 shader_ok=114 texture_ok=114
  ready=114`.
- Dominant remaining skip: `1344` `VS=0xB6C9863F710683EC /
  PS=0xA4A965C189287B99` no-fetch point draws classified as no modeled
  color/depth/stencil side effects.

Immediate blocker from this run:

- The first visible supported draw (`draw[24]`,
  `VS=0x1E6883FCCDE1F688 / PS=0xA4A965C189287B99`) decodes to screen-space
  vertices `(-0.5,-0.5)`, `(639.5,-0.5)`, `(639.5,359.5)`.
- Its pixel shader path currently returns `r0`; the captured color/fetch
  attribute for that draw is all zeros, while replay applies
  `rb_color_mask=0x0000FFFF`. The result is a large black primitive overwriting
  the native window.
- Therefore the next correctness target is not basic D3D12 initialization. It
  is Xenos shader/export semantics plus render-state/color-write interpretation
  for the early screen-space/depth-style passes, followed by broader constant
  layout reconstruction for later textured scene passes.

Follow-up gate added after this audit:

- The real D3D12 path now fails closed for the no-texture `PS=0xA4A965C189287B99`
  pass-through class. That shader is just the tiny `max(oC0, r0, r0)` export
  path; without a texture fetch, the current limited translator can only replay
  approximate `r0` data and repeatedly produced full-screen placeholder
  triangles.
- After this gate, `--real-backend-gaps` on the same capture reports
  `draws=1458 geometry_ok=26 shader_ok=26 texture_ok=26 ready=26`.
- The remaining ready draws are genuinely resource-dependent:
  `81311A/246E` (`8` draws), `AB1E86/246E` (`8` draws), `5D918D/C4ED`
  (`5` draws), and `AB1E86/C4ED` (`5` draws).
- Offline D3D12 replay on the gated capture submits `5` draws for
  `VS=0x5D918D91043B3ED0 / PS=0xC4ED2979F29C9139`, binds `20` captured texture
  SRVs, binds `20` captured samplers, uses `0` fallback textures, uses
  `0` diagnostic pipelines, and writes
  `native_captures\live_d3d12_strict_pathfix_001\d3d12-real-a4-notexture-gated.bmp`
  (SHA-256 `AF172ED5685D34C32696219E3C9E8630CDB8085341613BEFDBA2E3F85EF86FEB`).
  The image is a flat blue full-screen textured quad, not a BO2 scene.
- Strict live `native_d3d12` after this gate writes
  `native_captures\live_d3d12_a4_notexture_gate_001\desktop-a4-notexture-gate.png`
  (SHA-256 `A36FFDD0F794F28C64C818BD8157684C3337AB94E102A119BDBF7C7BA52A2383`).
  Telemetry reaches `submitted_frames=9` by frame `29`, and stdout reports
  repeated submissions with `diagnostic_pipelines=0` and `bound 8 captured
  texture SRV(s), 0 fallback`.

## 2026-07-01 live D3D12 audit

The current images are bring-up evidence, not proof of a complete native
renderer. The latest checked image is:

- `native_captures\live_d3d12_pending_001\d3d12-real-draw1061-textured.bmp`

That image is a flat dark resource-backed output. It is not a BO2 scene.

Code changes made in the project tree:

- `src\native_renderer\backend_d3d12\D3D12LiveFrameBuilder.*` now supports a
  pending frame builder. BO2 emits most PM4 work before the intercepted
  `VdSwap` frame boundary; previously the live backend reset its frame builder
  at `BeginFrame` and discarded those draws before D3D12 submission.
- `src\native_renderer\backend_d3d12\D3D12LiveRendererBackend.*` now routes
  shaders, constants, and draws seen outside a live frame into the pending
  builder, then absorbs them into the next native D3D12 frame.
- `src\native_renderer\replay\D3D12ReplayBackend.cpp` now transitions the live
  swapchain back buffer from `PRESENT` to `RENDER_TARGET` before clearing it.
- `native_renderer_live_allow_diagnostic_shader` was added as an explicit
  diagnostic-only live toggle. Strict `native_d3d12` still defaults to no
  diagnostic shader fallback.
- The live D3D12 backend now creates its own `BO2 Native D3D12` Win32 window
  instead of trying to attach a second DXGI swapchain to the existing game
  window. The previous path failed every frame at `CreateSwapChainForHwnd`.
- Captures now include `live_d3d12_submit` events with pending draw count,
  frame draw count, submit success/failure, cumulative submitted/failed frame
  counts, and the exact error string.

Verification:

```powershell
default.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_pending_001\events.jsonl --native_renderer_capture_limit 5000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose true --readback_resolve full
```

- The run was watchdog-killed after the capture limit window; it did not crash.
- The capture has `5000` events, `24` frame boundaries, `1134` PM4 draws,
  `232` shader payload uploads, `16` constant uploads, `84` vertex-buffer
  snapshots, `64` texture snapshots, and `1108` depth-target sidecar snapshots.
- `--real-backend-gaps` reports `geometry_ok=84`, `shader_ok=83`,
  `texture_ok=83`, `ready=83`.
- The dominant `VS=0xB6C9863F710683EC` / `PS=0xA4A965C189287B99` pair accounts
  for `1050` no-fetch/no-modeled-side-effect draws and is not scene output.
- The strongest textured probe is draw `1061`: indexed draw, six indices, one
  vertex fetch, six texture fetches, constants, and render state.

Replay probe:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_pending_001\events.jsonl --draw 1061 --backend d3d12 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_pending_001\d3d12-real-draw1061-textured.bmp --skip-unsupported --allow-diagnostic-shader --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache --no-summary
```

- Exit code: `0`.
- D3D12 submitted `2/2` captured draws for
  `VS=0x81311AC4B1FBD082` / `PS=0x246E20EF10E0DDC7`.
- Bound `8` captured texture SRVs and `8` captured sampler descriptors with no
  fallback SRVs/samplers.
- The sampled `tf0` sidecar payload for draw `1061` begins with repeated
  `0x10` bytes and is uniform enough that the current approximate HLSL produces
  a flat dark image. This is expected for that probe and does not prove
  scene-correct rendering.

What still needs to be done before calling the renderer real:

- Add live D3D12 submit telemetry to the capture/log stream so a run proves how
  many pending draws were absorbed and submitted per swapchain present.
- Capture or select a frame where the top ready textured draws reference
  non-uniform scene/color target payloads, not a uniform post-process surface.
- Replace the hand-written/limited `246E`, `C4ED`, `A4`, and related HLSL paths
  with real Xenos ALU/texture/export lowering driven by the semantic IR.
- Use captured constant register indices correctly instead of the current
  flattened `captured_constants[8]` approximation.
- Make live `native_d3d12` present a visible BO2-derived frame and record exact
  per-frame submit/failure counts.
- Keep `emulated` and capture modes working while live D3D12 is repaired.

### Live D3D12 telemetry after owned-window patch

Strict run:

```powershell
default.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_window_001\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
```

- Swapchain creation no longer fails.
- Pending draw absorption is working: typical frames report `pending_draws` and
  `frame_draws` in the `40-53` range.
- Strict live submit still fails because the per-frame batches do not yet have a
  draw with both supported geometry and a creatable real D3D12 PSO.

Diagnostic live run:

```powershell
default.exe --native_renderer_mode native_d3d12 --native_renderer_live_allow_diagnostic_shader true --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_diagnostic_window_001\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
```

- Frame `2` submitted successfully:
  `{"type":"live_d3d12_submit","frame":2,"pending_draws":0,"frame_draws":1,"attempted":true,"success":true,"submitted_frames":1,"failed_frames":1}`.
- That successful frame used diagnostic shader fallback and is not
  shader-correct BO2 rendering.
- Later frames fail after the D3D12 device is removed:
  `ID3D12Device::CreateCommittedResource(upload) failed with HRESULT 0x887A0005`
  and subsequent PSO creation calls fail with the same device-removed HRESULT.
- The next live backend target is to instrument `GetDeviceRemovedReason`, isolate
  the first live diagnostic draw that poisons the device, and either fix the
  invalid D3D12 state/resource transition or reset/recreate the live backend
  safely after device removal.

The working GPU-output backends are:

- `d3d12-diagnostic`, which renders synthetic debug rectangles from captured BO2 events.
- `d3d12`, which now renders the first resource-backed captured BO2 indexed draw from replay data using real captured vertex and index buffers with either a matching manual HLSL override pair or the explicitly requested temporary diagnostic shader fallback.
- `native_renderer_mode=native_d3d12`, which is now a distinct live mode in the game executable. It initializes a project-local D3D12 device/queue backend, suppresses emulated present, captures live PM4 events, and fails closed for live draw submission until the offline D3D12 replay translator is factored into the live path.

Neither path is full BO2 scene rendering yet. The `d3d12` path proves native D3D12 vertex/index buffer binding, manual override shader manifest parsing, compiled shader-cache hits, flattened captured constant root binding, captured format-6 texture SRV binding from inline or sidecar payloads, captured texture-filter/clamp sampler descriptors, first-pass captured PSO-side render-state setup, and `DrawIndexedInstanced` with captured BO2 geometry, but it still lacks automatic translated BO2 shaders, full constant-layout reconstruction, render-target/depth resources, and full-frame state replay. Without a matching cache entry, override, or translated shader, real `d3d12` fails closed.

## 2026-07-01 truth audit

`native_render_replay.exe` now has `--real-backend-gaps` to rank the blockers that prevent a capture from becoming real D3D12 scene output. This is intentionally stricter than the older "submitted draw" milestone.

`native_captures\vertex_recapture_001\events.jsonl`:

- `2415` draws total.
- `260` draws currently pass geometry, shader, and texture checks.
- `2090` draws are now blocked because the existing `VS=0xB6C9863F710683EC` / `PS=0xA4A965C189287B99` vertexless path initializes `r0` to zero and the pixel shader exports `r0`, so it only produces black placeholder output. This needs real Xenos register initialization semantics before it counts as scene rendering.
- `62` more no-fetch point draws using `VS=0xDDED7E538422AE73` / `PS=0x3A6876055FEC1674` are blocked for the same unresolved Xenos `r0`/register initialization class.
- Frame `3` after this gate submits only `2` non-placeholder captured draws and produces `native_captures\vertex_recapture_001\codex-audit-frame3-after-zero-gate-d3d12.bmp`, a large diagonal primitive. This is honest BO2-derived geometry, not a scene.

`native_captures\swap_fetch_capture_001\events.jsonl`:

- `196` draws total.
- `7` draws currently pass geometry, shader, and texture checks.
- The old `136` "decoded indices exceed captured vertex payloads" bucket was too vague. The replay gap report now identifies these as truncated vertex snapshots. Example: draw `135` decodes indices `456,457,458,458,459,456`, but its only captured vertex fetch has `payload=512/524288` bytes at `stride=32`, so replay only has `16` vertices for a draw that needs vertex `459`.
- The highest draw-count shader pairs have no usable geometry yet; the highest geometry-valid missing shader is `VS=0x6FC820AD382E1512` / `PS=0x679E60B50E703902`.
- `src/native_renderer/NativeRenderCaptureWriter.cpp` now writes large vertex payloads to `resources/vertex_payload_*.bin` sidecars instead of forcing them inline. `native_render_replay.exe` now loads `payload_resource_path` for vertex fetches and reports sidecar-backed vertex payloads in `--dump-vertices`.
- Existing captures do not magically gain the missing bytes. A new `native_capture` run is required before the top indexed scene draws can be replayed with full vertex buffers.

The next real-renderer work is therefore:

- Produce a fresh capture with vertex sidecars enabled and verify the `BA22484D6724E10F/ACAB60E35B237CD7` and `AC99F18664BC0EC1/604C8A462C3223AB` pairs no longer fail on truncated vertex payloads.
- Implement Xenos register initialization/export semantics for no-fetch point shaders instead of rendering zeroed `r0`.
- Expand real shader translation/cache coverage for the top geometry-valid shader pairs.
- Keep live `native_d3d12` fail-closed until it can submit the same real replay path without crashing or suppressing the emulated presenter into a blank frame.

## 2026-07-01 capture/replay correction

The images produced so far are not evidence of a complete renderer:

- `codex-visible-d3d12-diagnostic.bmp` is diagnostic event visualization, not BO2 scene output.
- `codex-visible-native-d3d12-replay.bmp` uses the real D3D12 replay path, but only the currently supported captured draws are submitted; missing full vertex payloads and missing shader translations leave it mostly black.
- `vertex_recapture_001\codex-audit-frame3-after-zero-gate-d3d12.bmp` is BO2-derived geometry, but it is still a partial replay, not a frame-correct scene.

New verification commands:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_001\events.jsonl --real-backend-gaps --top-shaders 12 --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\swap_fetch_capture_001\events.jsonl --draw 135 --dump-bound-state --dump-indices --dump-vertices --no-summary
```

Results after the replay diagnostic fix:

- `draws=196 geometry_ok=56 shader_ok=7 texture_ok=7 ready=7`.
- The gap report now emits examples such as `captured vertex payload is truncated; max_index=103 available_vertices=16` instead of lumping these under a generic index-range error.
- `native_render_replay` rebuilt successfully in `default\out\build\win-msvc-native-renderer-debug`.
- The installed RexGlue headers were stale relative to `C:\Users\braxt\rexglue-sdk\include`: the source tree has `NativeRenderer*Event` trace structs and `REX_PPC_NOINLINE`, while the installed copy did not. Refreshing `rex/ppc/function.h` and `rex/graphics/command_processor.h` from the source tree moved the game build past compilation.
- `default\out\build\win-msvc-amd64-relwithdebinfo\default.exe` rebuilt successfully after the header refresh.
- `default\out\build\win-msvc-native-renderer-debug` still fails at link time: installed `spdlogrd/fmtrd` libraries were built with release CRT settings while the target uses debug CRT, and the installed `rexruntimerd.lib` is stale and does not export `rex::graphics::CommandProcessor::SetNativeRendererTraceCallbacks`. A full RexGlue reinstall was attempted but fails in its current build tree because FFmpeg C files cannot find CRT headers such as `errno.h` and `stdio.h`.

## Real D3D12 gate

`native_render_replay.exe --backend d3d12` is reserved for the real resource-backed D3D12 backend. It renders only when a translated shader, compiled cache entry, or matching manual override is available. It refuses to render with the temporary shader unless `--allow-diagnostic-shader` is passed.

The fail-closed behavior is verified below with an intentionally empty override root.

Draw `1209` now has a hash-keyed manual D3D12 override pair:

- `shader_work\native_overrides\d3d12\vs_5D918D91043B3ED0.hlsl`
- `shader_work\native_overrides\d3d12\ps_C4ED2979F29C9139.hlsl`
- Manifest: `shader_work\native_overrides\overrides.json`

Verified command:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-constant-bound.bmp --no-summary
```

- Exit code: `0`
- Output: `native-renderer-d3d12-real-draw1209-constant-bound.bmp`
- SHA-256: `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Shader status: manual HLSL override loaded through `overrides.json`, compiled by the replay backend, cached as D3DCompile `.dxbc`, and bound with flattened captured constants at `b1`. This is not automatic Xenos translation and not DXC/DXIL yet.

Cache-only strict command:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-constant-bound-cache-only.bmp --no-summary
```

- Exit code: `0`
- Output SHA-256: `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`
- Evidence: succeeds with an empty override root because `shader_work\cache\shader_cache_index.json` maps the runtime hashes to compiled `.dxbc` blobs.

Supported shader-pair multi-draw command:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --d3d12-draws 64 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-supported-pair-multidraw-log.bmp --no-summary
```

- Exit code: `0`
- Backend log: `D3D12 real replay submitted 11 supported draw(s) for shader pair VS=0x5D918D91043B3ED0 PS=0xC4ED2979F29C9139 out of 11 captured draw(s) with that pair`
- Output SHA-256: `63031BF1F61F4E06E571428360D9DF93130E12AEE16FEAFC9CF9545F16C9EE60`

Strict missing-cache-and-override path:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --shader-override-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_overrides --shader-cache-root C:\Users\braxt\bo2-recompiled\native_captures\empty_shader_cache --no-summary
```

- Exit code: `1`
- Expected message includes `VS=0x5D918D91043B3ED0`, `PS=0xC4ED2979F29C9139`, and the selected override root.

The explicit diagnostic fallback still succeeds for captures with a supported indexed draw snapshot:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\events.jsonl --backend d3d12 --draw 1209 --allow-diagnostic-shader --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\shader_payload_capture_001\native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp --no-summary
```

- Output: `native-renderer-d3d12-real-draw1209-explicit-diagnostic.bmp`
- SHA-256: `B13E590D3818996F8B8A0C5B3E422D1541955A2D91D03C6AFC0CB7A5B464DB16`

The rendered geometry is the captured draw `1209` full-screen quad. The visible gradient comes from a diagnostic native shader using captured color/UV attributes, so this output is not shader-correct BO2 rendering.

## Current verified blocker

Draw `1209` in `native_captures\shader_payload_capture_001` has enough packet data, index bytes, constants, shader payloads, vertex fetch metadata, and bounded vertex bytes to identify a real indexed draw:

- Real: draw packet, index metadata, raw index bytes, decoded indices `3,0,2,2,0,1`, shader hashes and uploaded shader payload dwords, two constant ranges with payload, `vf95` at guest physical `0x0501E030`, stride `32`, attributes with Xenos formats `38`, `6`, and `37`, `128/128` raw vertex bytes, and decoded position/color/UV components.
- Implemented: D3D12 canonical input layout, upload-buffer vertex/index resources, and `DrawIndexedInstanced` for this draw.
- Implemented for the draw-1209 shader pair: shader cache-index lookup, manifest override lookup, HLSL compilation/cache write, flattened captured constants at `b1`, canonical D3D12 input layout, upload-buffer vertex/index resources, and `DrawIndexedInstanced` for all `11/11` captured draws with that pair.
- Latest capture-completeness evidence: `state_capture_004` validates `344` texture fetch snapshots, `1409024` texture payload bytes, and render-state records on `2475/2475` draws. Draw `799` carries the first complete packet set seen in replay: real indices, vertex fetch bytes, four texture fetch records, constants, and render state.
- Latest D3D12 texture/sampler evidence: real replay on `state_capture_004` submits the existing manual-override shader pair (`4/4` supported draws), reports `D3D12 real replay bound 4 captured texture SRV(s), 0 fallback texture SRV(s), unsupported_texture_attempts=0`, and reports `D3D12 real replay bound 4 captured sampler descriptor(s), 0 fallback sampler descriptor(s)`. Fresh `clamp_capture_001` validates `344/344` texture fetch records with clamp modes, and D3D12 replay reports `exact_clamp_modes=4, clamp_addressing_fallbacks=0` for the supported shader pair. The manual pixel override samples `t0` with `s0`, so captured format-6 texture data and captured texture filters/clamp modes are now in the D3D12 command stream.
- Latest sidecar texture evidence: fresh `sidecar_capture_002` validates `12000` events, `52` frames, `2628` draws, `412` texture fetch records, `372` texture sidecars, `3999744` sidecar bytes, and `sidecar_resource_manifest=present json=yes jsonl=yes`. D3D12 real replay submits `5/5` supported draws, binds `5` captured texture SRVs with `0` fallback SRVs, binds `5` captured sampler descriptors with `exact_clamp_modes=5`, and writes `native-renderer-sidecar-capture-002-d3d12-real.bmp`.
- Latest D3D12 render-state evidence: real replay on `state_capture_004` reports `D3D12 real replay applied render state from draw 799: color_mask=0x0000000F cull=2 depth_test=no depth_write=no stencil=no`. The replay PSO now consumes the captured rasterizer cull/front-face/fill/depth-clip subset, color write mask, blend factors/ops, and disabled depth/stencil state for the first supported draw.
- Latest present-resource evidence: fresh `frontbuffer_capture_001` validates `3000` events, `16` frames, `673` draws, and `15` PM4 swap frontbuffer sidecars totaling `55296000` bytes. The manifest parses with `15` `frontbuffer_payload` resources, each `3686400` bytes (`1280x720x4`). The first observed payloads are all zero, so the blocker has moved from no present payload capture to needing draw-time color/depth target payloads from the render-state base registers.
- Latest target-resource evidence: fresh `target_snapshot_capture_003` validates `1800` events, `10` frames, and `395` draws, with `368` color target sidecars and `369` depth target sidecars. Draw `29` records a corrected `1280x720x4` target footprint with `payload=16384/3686400` at `offset=1843200`. All scanned color/depth target sidecars are zero, which proves CPU target-base copies are not enough; the next target is ReXGlue/Xenos GPU render-target backing/readback or resolve instrumentation.
- Missing: automatic Xenos shader translation, DXC/DXIL compilation, full constant-layout reconstruction, GPU-side render-target/depth readback or resolve snapshots, texture formats/mips beyond the current decoded subset, full sampler LOD/mip validation, and full-frame draw sequencing against real BO2 frame boundaries.

## Next implementation targets

1. Expand sidecar-backed texture snapshots beyond 2D format `6`: mip footprints, compressed/packed formats, and additional captured formats.
2. Expand captured render-state handling: GPU-side color/depth target readback or resolve snapshots, real target descriptors, exact Xenos stencil-op translation, and complete blend coverage.
3. Replace flattened constant root data with layout-aware constant buffers from shader metadata.
4. Expand D3D12 real replay from one selected draw to all supported draws in the captured frame.
5. Add DXC/DXIL support and reuse the same cache index for translated shaders.
6. Add sidecar payload capture for larger vertex buffers. PM4 swap/frontbuffer sidecars are implemented, but draw-time render-target/depth snapshots are still missing.

## Frame replay status

The real D3D12 replay path now accepts `--strict` and `--skip-unsupported`. For `--frame N --backend d3d12`, the default behavior is strict/fail-closed: the first unsupported draw stops replay and reports the draw index, event id, shader hashes, and concrete unsupported reason. `--skip-unsupported` enables a bring-up mode that groups unsupported reasons and renders the first currently supported shader-pair batch.

Verified on `native_captures\vertex_recapture_001\events.jsonl`:

- Strict command: `native_render_replay.exe --capture native_captures\vertex_recapture_001\events.jsonl --frame 0 --backend d3d12 --shader-override-root empty_shader_overrides --shader-cache-root shader_work\cache --d3d12-output native-renderer-frame0-strict-d3d12.bmp --no-summary`
- Strict result after vertexless B6/A4 and no-side-effect elision: exit `1`, `D3D12 frame replay strict failure at frame 0 draw 1967 event 1968 VS=0xDDED7E538422AE73 PS=0x3A6876055FEC1674: DDED7E no-fetch point shader needs Xenos r0/register initialization semantics before D3D12 can draw it`.
- Skip command: `native_render_replay.exe --capture native_captures\vertex_recapture_001\events.jsonl --frame 0 --backend d3d12 --skip-unsupported --shader-override-root empty_shader_overrides --shader-cache-root shader_work\cache --d3d12-output native-renderer-frame0-skip-d3d12.bmp --no-summary`
- Initial skip result: exit `0`, `frame_draws=2415`, `geometry_supported=260`, `skipped=2155`, `rendered_current_shader_pair=120`, output `native-renderer-frame0-skip-d3d12.bmp`.
- Multi-pair skip result: exit `0`, `frame_draws=2415`, `geometry_supported=260`, `skipped=2155`, `submitted_supported_draws=260`, output `native-renderer-pair-frame0-d3d12.bmp`. The replay submits all geometry-supported real BO2-derived draws across `7` shader pairs: `1E6883/A4A965`, `5B9B/3A68`, `5D918D/C4ED`, `81311A/246E`, `AB1E86/246E`, `AB1E86/A4A965`, and `AB1E86/C4ED`. The only remaining skips in this capture are `2155` draws with no captured vertex/fetch state.
- Captured-state PSO-key result: exit `0`, `260` submitted draws, output `native-renderer-pair-frame0-d3d12.bmp`, `PSO cache: entries=10 misses=10 hits=250`. The key includes shader hashes, expanded topology, render target format, captured depth/color formats, blend/depth/stencil/raster registers, decoded depth/stencil/cull/fill/front-face flags, color mask, and MSAA sample count.
- Phase 1 input/resource result: exit `0` for `--draw 24 --d3d12-draws 128`, `120/120` submitted, output `native-renderer-phase1-draw24-d3d12.bmp`, and `PSO cache: entries=3 misses=3 hits=117`, proving the same shader pair now splits by captured state/input-layout keying. Frame skip replay now submits `260` draws across `7` shader pairs.
- Real texture strictness result: `--draw 909 --d3d12-draws 32 --backend d3d12` exits `0`, submits `26/26` draws for `81311A/246E`, binds `104` captured texture SRVs and `104` captured samplers, and reports `0` texture fallbacks. Unsupported captured texture fetches now fail strict real replay or are counted/skipped only under frame `--skip-unsupported`.
- DSV status: real D3D12 replay now creates, clears, and binds an offscreen `D24_UNORM_S8_UINT` DSV for real draws. Captured depth test/write flags, depth compare, stencil enable, stencil masks, and stencil ref now feed the PSO/stencil state. Verified `--draw 24 --d3d12-draws 128 --backend d3d12` submits `120/120` draws and reports `depth_enabled_draws=82 depth_write_draws=82 stencil_enabled_draws=120`; verified frame skip replay submits `260` supported draws across `7` shader pairs. This is still not captured BO2 depth contents, and stencil ops are currently conservative `KEEP` operations until the Xenos op fields are decoded.
- Vertexless B6/A4 status: the strict real backend now has a narrow, evidenced path for non-indexed point-list draws with no captured vertex fetches only when the shader pair is `VS=0xB6C9863F710683EC` / `PS=0xA4A965C189287B99`. It uses a no-input `SV_VertexID` VS variant, binds no vertex buffer, and leaves enabled-output no-fetch draws unsupported. On `native_captures\vertex_recapture_001\events.jsonl`, direct draw `0` submits `2090/2090` draws for this pair, and frame skip replay submits `2350` supported draws across `8` shader pairs with `3` no-side-effect draws elided and `62` enabled-color `DDED7E/3A6876` no-fetch point draws still skipped pending Xenos `r0`/register initialization decoding.
- Capture-boundary note: this capture has `46` frame markers, but every PM4 draw is currently classified before those markers. Frame `0` therefore uses an explicitly logged pre-frame draw bucket fallback for this capture only. The next capture/replay targets are assigning PM4 draws to actual frame ranges and replacing the offscreen placeholder target/depth resources with captured BO2 target/depth resources.
# Current audit - 2026-07-01

The native renderer is not complete. The current D3D12 output is BO2-derived
resource replay, not a real BO2 scene and not live native scene rendering.

Latest live capture stability work:

- Fixed an unsafe Windows import-thunk hook in project code. `__imp__VdSwap`
  and `__imp__VdSetSystemCommandBufferGpuIdentifierAddress` are 6-byte COFF
  jump thunks in the MSVC build; the previous 12-byte code patch corrupted the
  adjacent thunk and crashed at `default.exe+0x7F203D2` with `0xC000001D`.
  `InstallImportThunkDetour` now patches the IAT slot instead.
- Added a SEH-safe `VdSwap` forward guard. If the native hook sees an invalid
  command buffer, fetch constant, or scalar pointer, it suppresses RexGlue
  forwarding for that present call instead of letting `VdSwap_entry` fault
  while reading the fetch constant.
- Verified `default` RelWithDebInfo rebuilds after the import-thunk fix.

Fresh capture evidence:

```powershell
default.exe --native_renderer_mode native --native_renderer_shader_record_probe_mode off --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\iat_vdswap_capture_002\events.jsonl --native_renderer_capture_limit 8000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --readback_resolve full
```

- Run was stopped by watchdog after 75 seconds, not by a crash.
- Capture validates with `8000` events, `36` frames, `1759` PM4 draws,
  `394` shader uploads, `51` constant uploads, `19` index-buffer snapshots,
  `147` vertex-buffer snapshots, `204` texture snapshots, `35` PM4 swap
  frontbuffer sidecars, and frame markers (`begin_frame`, `vd_swap`,
  `end_frame`, `present_snapshot`).
- D3D12 real-backend gap report: `draws=1759 geometry_ok=147 shader_ok=147
  texture_ok=147 ready=147`.
- Dominant skip reason after replay reclassification: `1612` draws for
  `VS=0xB6C9863F710683EC / PS=0xA4A965C189287B99` have no captured
  vertex/fetch state and, in this capture, no modeled color/depth/stencil side
  effects. They should not be counted as rendered scene output.

Latest D3D12 replay evidence:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\iat_vdswap_capture_002\events.jsonl --backend d3d12 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\iat_vdswap_capture_002\d3d12-real-auto.bmp --skip-unsupported --allow-diagnostic-shader --no-summary
```

- Exit code: `0`.
- Output: `C:\Users\braxt\bo2-recompiled\native_captures\iat_vdswap_capture_002\d3d12-real-auto.bmp`.
- Log: submitted `92/92` supported draws for shader pair
  `VS=0x1E6883FCCDE1F688 / PS=0xA4A965C189287B99`.
- Log: `0` captured texture SRVs were bound and `368` fallback texture SRVs
  were used for this selected shader-pair batch.
- The image is a dark triangle/plane. It proves the D3D12 replay path can
  consume captured BO2 vertex/render/depth state for supported draws, but it is
  still not shader-correct or scene-correct BO2 rendering.
- Direct textured draw check:
  `native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\iat_vdswap_capture_002\events.jsonl --draw 1117 --backend d3d12 --d3d12-output C:\Users\braxt\bo2-recompiled\native_captures\iat_vdswap_capture_002\d3d12-real-draw1117-textured.bmp --skip-unsupported --allow-diagnostic-shader --no-summary`
  exits `0`, submits `13/13` draws for
  `VS=0x81311AC4B1FBD082 / PS=0x246E20EF10E0DDC7`, binds `52` captured
  texture SRVs, binds `52` captured sampler descriptors, and uses `0` fallback
  SRVs.

Immediate remaining work:

1. Bind captured textures for the auto-selected supported shader pairs instead
   of falling back to placeholder SRVs.
2. Implement Xenos no-fetch point shader register initialization for the
   dominant `B6C986/A4A965` draw class.
3. Replace diagnostic/manual shader paths with translated shader IR/HLSL for
   the runtime-used shader pairs.
4. Improve frame bucketing so PM4 draws are assigned to the actual `VdSwap`
   frame ranges instead of living mostly in the pre-frame bucket.
5. Move the same replay translator into live `native_d3d12`; current live mode
   still captures/mirrors data and does not render the final BO2 scene
   natively.

# Current visual audit - 2026-07-01 follow-up

The newest live native D3D12 window is still not real BO2 scene rendering.

Evidence:

- Live native screenshot:
  `C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_a4_notexture_gate_001\desktop-a4-notexture-gate.png`.
  The image is a flat blue full-screen triangle/quad in the owned
  `BO2 Native D3D12 - default` window.
- The live log reports real native submission, not diagnostic fallback:
  `D3D12 real frame replay submitted 2 supported draw(s) across 2 shader
  pair(s)`, `diagnostic_pipelines=0`, `bound 8 captured texture SRV(s), 0
  fallback texture SRV(s)`.
- `--real-backend-gaps` on the capture reports `draws=1403 geometry_ok=24
  shader_ok=24 texture_ok=24 ready=24`, but the ready draws are still
  full-screen/postprocess-style draw classes:
  `5D918D/C4ED`, `81311A/246E`, `AB1E86/246E`, and `AB1E86/C4ED`.
- Direct replay of draw `1112`
  (`VS=0x81311AC4B1FBD082 / PS=0x246E20EF10E0DDC7`) submits `6/6` real draws,
  binds `24` captured texture SRVs, binds `24` captured samplers, uses
  `0` diagnostic pipelines, and writes
  `C:\Users\braxt\bo2-recompiled\native-renderer-sparse-constants-draw1112-d3d12.bmp`.
  The image is still a uniform dark red/brown full-screen output, not a scene.
- Raw texture sidecar
  `native_captures\live_d3d12_a4_notexture_gate_001\resources\texture_payload_00002208.bin`
  visualized as `native-renderer-texture2208-gray.bmp` is mostly flat bands,
  so the currently selected textured pass is not a useful world-color scene
  input.

Fix applied in this audit:

- `src/native_renderer/replay/D3D12ReplayBackend.cpp` now preserves captured
  PM4 constants in a sparse Xenos float4 register map instead of only
  concatenating upload records. This is required because runtime shaders
  reference registers such as `c233`, `c234`, and `c252..c255`; a flat
  `captured_constants[0..]` layout makes translated shaders read the wrong
  constants.
- `--real-backend-gaps` now separates fullscreen utility/postprocess passes
  from scene candidates. On
  `native_captures\live_d3d12_a4_notexture_gate_001\events.jsonl`, the report
  now prints `ready=24 utility_ready=24 scene_candidate_ready=0`. This means
  every currently submit-ready draw in this capture is a utility/postprocess
  fullscreen pass, not a proven BO2 scene/world draw.
- RelWithDebInfo `default` and `native_render_replay` rebuilt successfully
  after the sparse constant-map and gap-classifier changes.

Remaining blocker after this audit:

The renderer is still blocked on real shader and scene-pass reconstruction, not
basic D3D12 binding. The current generated HLSL for
`PS=0x246E20EF10E0DDC7` is explicitly a limited semantic approximation: it
samples captured textures and applies a hand-written luma/bias formula rather
than lowering the actual Xenos ALU, predicate, scalar, and export sequence. The
IR/disassembly for that shader contains the real operations through final
`mad oC0.xyz1`, and those operations must be lowered into HLSL/SPIR-V against
the sparse constant map before this pass can be considered shader-correct.

Immediate next targets:

1. Add replay classification for supported postprocess/utility passes versus
   scene/world geometry so `--real-backend-gaps` no longer makes a full-screen
   utility quad look like renderer completion.
2. Lower the full `246E20EF10E0DDC7` pixel shader operation sequence, including
   register sources, swizzles, modifiers, predicate flow, scalar ops, and
   `oC0` export.
3. Expand capture/replay evidence to find a draw batch with real scene/world
   geometry and non-flat texture/color inputs. The latest capture does not yet
   prove that such a scene batch is being rendered natively.

# MP native renderer audit - 2026-07-01

The MP target now builds and runs long enough in both `emulated` and
`native_d3d12` modes under watchdog control. The original immediate MP crash was
not a native-renderer crash: the hand-linked executable crashed in `emulated`
mode at `0xC0000005` because the installed ReXGlue runtime library was stale
and lacked `CommandProcessor::SetNativeRendererTraceCallbacks`. ReXGlue was
rebuilt and installed from commit `3b37645` after fixing the VS 18
`std::chrono::clock_cast` issue in `src/core/threading_win.cpp`.

MP capture evidence:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --native_renderer_capture_limit 6000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

- Run was stopped by watchdog after 25 seconds, not by a crash.
- Capture: `C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl`.
- Summary: `6000` events, `22` frames, `1179` PM4 draws, `439` shader uploads,
  `110` constant uploads, `108` index snapshots, `215` vertex snapshots,
  `224` texture snapshots, `1135` color target snapshots, and `1153` depth
  target snapshots.
- The MP render-target/depth snapshot counts were fixed by copying
  `NativeRendererRenderStateEvent` color/depth payload fields in
  `default_mp/src/native_renderer_hooks.cpp`. Before that fix, MP captures had
  zero color/depth snapshots even though ReXGlue was capturing them.

D3D12 replay evidence:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-auto-skip-d3d12-real-2.bmp
```

- Exit code: `0`.
- Output: `C:\Users\braxt\bo2-recompiled\native-renderer-mp003-auto-skip-d3d12-real-2.bmp`.
- Log: submitted `25` real D3D12 draws across one translated/cached shader
  pair, `VS=0x5B9B7484417FB9B6 / PS=0x3A6876055FEC1674`.
- Log: `diagnostic_pipelines=0`; this output is not the diagnostic shader path.
- The image is sparse colored point/marker output, not a BO2 scene. It proves
  the MP path can capture and replay real BO2 vertex/constants/render-target
  state through D3D12, but it does not prove scene-complete native rendering.

Current MP blockers:

1. `--real-backend-gaps` on MP003 reports `ready=25 scene_candidate_ready=25`,
   but the output is still sparse and likely not a useful world scene pass.
2. `942` draws are no-fetch/no-side-effect utility draws that still need
   modeled Xenos export/register semantics or should be filtered as non-scene.
3. Missing shader translations remain for runtime-used pairs including
   `VS=0x3C4F6D40D699817B / PS=0xEDC17DCC3FFDB040`,
   `VS=0xCBC9604F48930B36 / PS=0x7D1EF030F5710BDA`,
   `VS=0x261BDD733FEC1F64 / PS=0xFF01D28E1EF3A880`, and related variants.
4. Texture sidecars are captured, but the currently replayed MP ready batch has
   no texture fetches, so MP has not yet proven textured scene output.
5. Live `native_d3d12` still needs the same translated-scene draw coverage as
   offline replay before it can be called a complete native renderer.

# MP textured UI shader pass update - 2026-07-01

Added limited native D3D12 coverage for the MP capture shader pair
`VS=0x3C4F6D40D699817B / PS=0xEDC17DCC3FFDB040`.

Evidence:

```powershell
native_shader_inspect.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --hash 0x3C4F6D40D699817B --semantic-disassemble --write-semantic shader_work\cache\disasm --write-semantic-ir shader_work\cache\ir --write-translated-hlsl shader_work\cache\hlsl --compile-translated-hlsl-dxc shader_work\cache
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-after-3c4f-screenspace-d3d12-real.bmp
```

- `VS=0x3C4F6D40D699817B` now has a generated limited HLSL/DXIL cache entry.
  The semantic disassembly shows one `vf95` stream with
  `FMT_32_32_32_32_FLOAT` position, `FMT_8_8_8_8` color, and
  `FMT_32_32_FLOAT` UV attributes.
- `PS=0xEDC17DCC3FFDB040` now has a manual D3D12 override matching the decoded
  Xenos sequence `tfetch2D r0, r0.xy, tf0` followed by `mul o0, r0, r1`.
- The D3D12 texture decoder now tolerates a small missing tail in otherwise
  complete format-6 snapshots. MP003 contains 1024x1024 format-6 sidecars that
  are 128 bytes short of the computed linear footprint; rejecting the whole
  resource prevented otherwise valid draws from binding.
- Strict D3D12 replay now submits `71` real captured draws across two shader
  pairs, up from `25`:
  - `46/46` draws for `VS=0x3C4F6D40D699817B /
    PS=0xEDC17DCC3FFDB040`
  - `25/25` draws for `VS=0x5B9B7484417FB9B6 /
    PS=0x3A6876055FEC1674`
- The `3C4F/EDC1` path binds real captured textures and samplers:
  `46` captured texture SRVs, `46` captured sampler descriptors,
  `diagnostic_pipelines=0`.
- Draw 29 vertex evidence is sane for UI geometry: positions
  `(928.4,594)` to `(1184,666)`, UVs `0..1`, white vertex color, six real
  indices, and a captured 256x64 DXT texture snapshot.

Important limitation:

The output image is still not a correct BO2 frame. The full `3C4F/EDC1` batch
currently produces a flat gray target, and a single draw 29 replay produces a
black rectangle over the clear target. This means the renderer has improved
real resource/shader coverage, but not visual correctness. The next concrete
targets are:

1. Add exact single-draw D3D12 replay diagnostics that do not silently collect
   compatible later draws unless requested.
2. Dump decoded texture previews for the bound DXT/format-6 UI textures and
   compare against expected BO2 UI assets.
3. Verify the pixel shader export/register mapping for `mul o0, r0, r1`; the
   semantic analyzer reports `writes_color_targets=0`, so `o0` may need a
   stage-specific export mapping rather than being assumed as `SV_Target0`.
4. Continue with the next missing scene shader pairs:
   `VS=0xCBC9604F48930B36 / PS=0x7D1EF030F5710BDA`,
   `VS=0x261BDD733FEC1F64 / PS=0xFF01D28E1EF3A880`, and
   `VS=0xEF95534343684F5B / PS=0xDC168FB6031AFC41`.

# MP textured replay visual fix - 2026-07-01

The `3C4F/EDC1` path now produces visible BO2-derived texture output in
offline D3D12 replay.

Evidence:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --draw 29 --dump-texture --texture-slot 0 --texture-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-draw029-texture0-tiledbc.bmp --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --draw 29 --backend d3d12 --d3d12-draws 1 --shader-cache-root C:\Users\braxt\bo2-recompiled\shader_work\cache_real_uvfix --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-draw029-real-uvfix.bmp
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-full-real-uvfix-defaultcache.bmp
```

- Added `--dump-texture`, `--texture-slot`, and `--texture-output` to the
  replay CLI. The command uses the same D3D12 texture decoder as real replay,
  so texture preview output validates the renderer path instead of a separate
  ad hoc parser.
- Fixed tiled DXT/DXN decode by applying the Xenos tiled address function to
  compressed block coordinates when the fetch constant is marked tiled. Draw 29
  texture slot 0 changed from striped/garbled output to a readable
  `CALL OF DUTY BLACK...` logo texture preview.
- Fixed canonical vertex UV assignment. The old code used the global input
  layout mask to choose `uv` versus `uv1`, so after vertex 0 every later vertex
  wrote its first UV set to `uv1` and left `uv` as zero. That caused textured
  quads to sample only the top-left texel. The builder now tracks texcoord
  assignment per vertex.
- Single draw 29 now renders a visible native D3D12 UI texture rectangle into
  `native-renderer-mp003-draw029-real-uvfix.bmp`.
- Full strict replay with the default cache submits `71` real captured draws
  across two shader pairs, with `46` captured texture SRVs and
  `diagnostic_pipelines=0`, and produces visible BO2-derived UI/background
  content in `native-renderer-mp003-full-real-uvfix-defaultcache.bmp`.

Remaining limitations:

- The image is still not a full/correct BO2 scene frame. Some textures are
  visibly incomplete or incorrectly ordered, and only the current UI/background
  shader classes are translated/overridden.
- Live `native_d3d12` still needs to be rerun with these fixes and compared
  against offline replay.
- World/scene shader pairs and render/depth state remain the next major
  coverage gap.

# MP003 PS 79E1 no-texture bridge - 2026-07-01

Draw `936` was inspected as the representative `PS=0x79E1F538A5074A65`
blocker. It has real decoded indices `3,0,2,2,0,1`, one vf95 vertex fetch
with four screen-space vertices covering `y=540..720`, no texture fetches,
captured vertex color `(1,1,1,0.101961)`, and captured render state. The
shader payload is `1293` dwords and currently times out in semantic
disassembly, so `shader_work/native_overrides/d3d12/ps_79E1F538A5074A65.hlsl`
is a conservative manual override that preserves captured vertex color/alpha
without adding a synthetic texture or diagnostic color.

Verification:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --draw 936 --backend d3d12 --d3d12-draws 1 --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-draw936-79e1.bmp --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-full-after-79e1.bmp
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --real-backend-gaps --no-summary
```

- Draw `936` submits `1` real D3D12 draw for
  `VS=0xCBC9604F48930B36 / PS=0x79E1F538A5074A65` with
  `diagnostic_pipelines=0`.
- Full MP003 replay submits `133` supported real-data draws across `7` shader
  pairs, up from `130` across `6` pairs. The new pair is
  `CBC9604F48930B36/79E1F538A5074A65`, `3/3` submitted.
- The gap report now shows `geometry_ok=152 shader_ok=152 texture_ok=152
  ready=152`, with no remaining shader-only blocker. Remaining blockers are
  geometry/no-fetch semantics: `942` no-side-effect no-fetch draws, `63`
  A4 pass-through draws lacking reliable color/texture dependency, and `22`
  `DDED7E` no-fetch point draws needing Xenos register initialization.

# MP003 no-fetch utility and A4 depth/stencil accounting - 2026-07-01

The D3D12 real-backend gap report now separates no-raster/no-side-effect
utility traffic from scene-candidate blockers instead of counting it as missing
rendering. This does not submit synthetic geometry; it only changes analysis
classification.

Rules added:

- No-fetch draws with color writes disabled and depth/stencil disabled are
  reported as `ignored utility/no-raster no-fetch draw`.
- The exact decoded `VS=0xDDED7E538422AE73` no-fetch point class is also
  ignored as no-raster utility evidence. Its semantic IR has no constants, no
  vertex fetches, no texture fetches, and exports position through
  `sqrt oPos, -r_abs[0].x` after `setp_clr`.
- `PS=0xA4A965C189287B99` zero-color/pass-through draws remain blocked for
  pure color-only passes, but are now allowed when captured render state uses
  depth or stencil. Those passes have real side effects even if the color
  export is zero.

Verification:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --real-backend-gaps --no-summary
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_003\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp003-full-after-a4-depth.bmp
```

Results:

- Gap report: `geometry_ok=203 shader_ok=203 texture_ok=203 ready=203`,
  `ignored_utility=964`.
- Full MP003 D3D12 replay submits `184` supported real-data draws across `8`
  shader pairs with `diagnostic_pipelines=0`.
- The newly admitted pair is `VS=0x1E6883FCCDE1F688 /
  PS=0xA4A965C189287B99`, `51/51` submitted.
- Depth/stencil evidence is now visible in backend output:
  `depth_target_bound=yes`, `depth_enabled_draws=31`,
  `depth_write_draws=31`, `stencil_enabled_draws=51`, and nonzero depth
  readback `native-renderer-mp003-full-after-a4-depth-depth.bmp`.
- The remaining `12` pure color-only
  `VS=0xAB1E86137A0240E8 / PS=0xA4A965C189287B99` draws were tested two ways.
  Passing position/color-derived data into A4's `r0` produced an untrusted gray
  diagonal fullscreen-triangle artifact in
  `native-renderer-mp003-full-after-ab1e-a4.bmp`. The kept path uses a
  pair-specific zero-output pixel shader because AB1E semantic IR exports
  position only and does not declare an interpolator for A4's `r0` input.
  Single draw `25` now submits with `diagnostic_pipelines=0` and writes
  `native-renderer-mp003-draw25-ab1e-a4-zero.bmp`.
- With the AB1E/A4 zero-output pair variant, MP003 gap reporting has no active
  blockers for the currently modeled classes:
  `geometry_ok=215 shader_ok=215 texture_ok=215 ready=215`,
  `scene_candidate_ready=203`, `utility_ready=12`, and
  `ignored_utility=964`. Full replay submits `196` supported real-data draws
  across `9` shader pairs, including `12/12` for `AB1E/A4`, with
  `diagnostic_pipelines=0`, output
  `native-renderer-mp003-full-after-ab1e-a4-zero2.bmp`.
- Ghidra MCP evidence for XEX `0x825828D8` supports the no-fetch utility
  classification: the function emits a fixed PM4 sequence with embedded shader
  payload and `PM4_DRAW_INDX_2` packet rather than binding normal scene
  vertex/index resources.

# MP live AB1E texture interpolator blocker - 2026-07-02

Strict live MP `native_d3d12` was rerun from the rebuilt RelWithDebInfo
`default_mp.exe`:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_004\events.jsonl --native_renderer_capture_limit 6000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

Evidence:

- The run was watchdog-limited and killed after 75 seconds by design, not hung.
- Live `native_d3d12` submitted real D3D12 frames from the MP command stream.
  Representative live frame logs show `19..25` submitted real-data draws,
  captured texture/sampler binding, depth target binding, and
  `diagnostic_pipelines=0`.
- Offline replay of the new live capture submits `195` real-data draws across
  `9` shader pairs with nonzero color/depth readback:
  `native-renderer-mp004-live-replay-d3d12.bmp`.
- Gap analysis over the same capture still finds `215` modeled ready draws, but
  the extra `20` are `AB1E/EDC1` and `AB1E/FF01` texture passes.

The `AB1E` texture pairs are now explicitly blocked in real D3D12 replay rather
than allowed to reach PSO creation:

- `VS=0xAB1E86137A0240E8` semantic disassembly:
  `vfetch_full r0.xy11`, `alloc interpolators`, `alloc position`,
  `max oPos, r0, r0`, with `writes_interpolators=0x00000000`.
- `PS=0xFF01D28E1EF3A880` semantic disassembly:
  `tfetch2D r1.1w__, r1.xy, tf1`, `mul o0, r1.xxxy, r0`,
  with `writes_interpolators=0x00000001`.
- `PS=0xEDC17DCC3FFDB040` semantic disassembly:
  `tfetch2D r0, r0.xy, tf0`, `mul o0, r0, r1`,
  with `writes_interpolators=0x00000001`.

A temporary screen-UV bridge was tested and rejected: it made the backend submit
all `215` modeled draws but produced a large gray diagonal triangle over the MP
UI/background output. This proves the remaining blocker is real Xenos
interpolator/register semantics for AB1E texture passes, not missing texture or
vertex payload capture. The current strict real path keeps these draws skipped
with an explicit blocker until the real mapping is decoded.

# MP DXT texture recapture fix - 2026-07-02

The MP native capture path now recaptures block-compressed texture payloads
from guest physical memory instead of accepting the ReXGlue/Xenos 16 KB preview
as a real resource. The fixed formats are:

- `18` / DXT1-style blocks
- `19` / DXT2/3-style blocks
- `20` / DXT4/5-style blocks
- `49` / DXN-style two-channel blocks

The hook uses a tiled address upper bound for both uncompressed and
block-compressed resources. This matters because the Xbox tiled address of the
last logical block is not always the maximum byte touched by the texture.

Validation capture:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_009\events.jsonl --native_renderer_capture_limit 3000 --native_renderer_capture_flush_interval 128 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

The run was watchdog-killed after 30 seconds by design. Live logs show
`diagnostic_pipelines=0`, `unsupported_texture_attempts=0`, and
`partial_texture_previews=0`.

Offline gap result for MP009:

```text
draws=648 geometry_ok=50 shader_ok=50 texture_ok=50 ready=50
ignored_utility=586
top blocker: 12 x AB1E texture pass pixel shader reads interpolator r0/r1,
but the captured AB1E vertex shader writes oPos only
```

Full real D3D12 replay:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_009\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp009-full-dxt-fixed.bmp --no-summary
```

Result:

- `50` real supported draws submitted across `4` shader pairs.
- `24` captured texture SRVs bound.
- `unsupported_texture_attempts=0`.
- `partial_texture_previews=0`.
- `diagnostic_pipelines=0`.
- `native-renderer-mp009-pair-3c4f-edc1.bmp` correctly shows the BO2 title
  logo from real captured DXT texture data, replacing the prior green/pink
  upper-left smear caused by truncated DXT previews.

This is still not complete native rendering. The combined MP009 image has real
BO2-derived logo/UI texture output, but still has missing layers and a large
dark diagonal/state artifact. The next blocker remains Xenos
interpolator/register semantics for AB1E texture passes, followed by remaining
depth/stencil/fullscreen pass correctness.

# MP A4 color-export fail-closed update - 2026-07-02

The large dark diagonal in `native-renderer-mp009-full-dxt-fixed.bmp` was
isolated to no-texture `PS=0xA4A965C189287B99` passes, not to the fixed DXT
title texture pair.

Pair isolation on `native_captures\live_d3d12_mp_009\events.jsonl` showed:

- `VS=0xAB1E86137A0240E8 / PS=0xA4A965C189287B99`: `2` submitted draws
  reproduced the diagonal.
- `VS=0x1E6883FCCDE1F688 / PS=0xA4A965C189287B99`: `24` submitted draws
  also reproduced the diagonal, with captured depth/stencil state enabled.
- `VS=0x3C4F6D40D699817B / PS=0xEDC17DCC3FFDB040`: `12` submitted draws
  rendered the BO2 title/logo texture without the diagonal.
- `VS=0xEF95534343684F5B / PS=0xDC168FB6031AFC41`: `12` submitted draws
  rendered the companion textured UI/title pass without the diagonal.

The replay state dump now decodes key render-state fields for these draws,
including `rb_colorcontrol`, `rb_modecontrol`, `rb_blendcontrol[0]` factor/op
fields, raster flags, screen/window scissor, and viewport registers. The AB1E
texture draw `30` decodes `rb_blendcontrol[0]=0x010B0706` as
`SRC_ALPHA ADD INV_SRC_ALPHA` for color and `INV_DEST_ALPHA ADD ONE` for alpha,
with a `1280x720` window scissor. This proves the draw is a real blended
fullscreen/UI pass, but not that the current limited shader interface can
replay its missing interpolators correctly.

The D3D12 real backend now keeps no-texture A4 color-export passes fail-closed.
These shaders are `max(oC0, r0, r0)` style exports. Without a texture fetch, the
current translator can only use an approximate source for `r0`, and prior
attempts have repeatedly produced false fullscreen triangles. This applies even
when the captured render state has depth/stencil side effects, because offline
replay is still using synthetic replay depth/target resources rather than the
real BO2 target contents for those passes.

Updated MP009 full replay:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_009\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp009-a4-failclosed.bmp --no-summary
```

Result:

- `24` real supported draws submitted across `2` shader pairs.
- Submitted pairs are the two textured title/UI pairs:
  `3C4F6D40D699817B/EDC17DCC3FFDB040` and
  `EF95534343684F5B/DC168FB6031AFC41`.
- `24` captured texture SRVs bound.
- `unsupported_texture_attempts=0`.
- `partial_texture_previews=0`.
- `diagnostic_pipelines=0`.
- The output image still is not a complete BO2 frame, but the false dark
  diagonal is gone and the visible BO2-derived title texture remains.

Updated gap report:

```text
draws=648 geometry_ok=24 shader_ok=24 texture_ok=24 ready=24
ignored_utility=586
top blockers:
  26 x A4 pass-through color export has no captured color/texture dependency
  12 x AB1E texture pass needs Xenos interpolator/register semantics
```

Next renderer work remains the same core issue: decode the Xenos
export/interpolator/register semantics for A4/AB1E/fullscreen utility passes,
then replace the synthetic offline target/depth handling with real captured
render-target/depth state so those passes can be replayed instead of skipped.

# Opt-in full render-target snapshots - 2026-07-02

The MP capture hook can now recapture full color/depth target payloads from
guest physical memory without modifying `rexglue-sdk`. The implementation uses
the captured Xenos target base as a 4 KB page base (`base << 12`) and writes a
full sidecar payload with offset `0`.

This is gated behind:

```powershell
$env:BO2_NATIVE_CAPTURE_FULL_TARGETS='1'
```

The gate is required because full target snapshots on every draw can consume
disk very quickly. Normal captures keep the smaller ReXGlue preview payloads.

Focused validation capture:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\target_full_mp_001\events.jsonl --native_renderer_capture_limit 180 --native_renderer_capture_flush_interval 32 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

Run result: watchdog-killed after 12 seconds by design. Capture size was
approximately `14.2 MB`.

Resource summary:

```text
color_target_snapshots=4 color_missing=0 color_payload_bytes=11059200
color_target_payload_truncated=0
depth_target_snapshots=2 depth_missing=0 depth_payload_bytes=3686400
depth_target_payload_truncated=0
```

Draw `24` (`VS=1E6883FCCDE1F688 / PS=A4A965C189287B99`) now reports:

```text
color_target_payload rt=0 base=1328 offset=0 payload=1843200/1843200 sidecar
depth_target_payload base=1328 offset=0 payload=1843200/1843200 sidecar
```

This removes a capture-completeness blocker for early A4 depth/stencil passes.
It does not by itself make those passes renderable yet, because the D3D12
offline replay still needs to initialize/bind captured target/depth payloads as
real replay resources and still needs correct Xenos export/register semantics
for the A4/AB1E shader classes.

# D3D12 color-target seeding - 2026-07-02

The offline D3D12 real replay path can now initialize its output render target
from a captured color target sidecar before it emits native draw calls. This is
fail-closed: the seed is accepted only when target slot `0` is a complete,
non-truncated, offset-`0`, raw linear RGBA8 payload whose dimensions match the
replay output target. Otherwise the backend logs the exact rejection reason and
keeps the previous clear behavior.

Validation on the current MP009 capture:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_009\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-mp009-targetseed-regression.bmp --no-summary
```

Result:

```text
D3D12 real replay submitted 24 supported draw(s) across 2 shader pair(s)
D3D12 real replay color target initialized by clear: no usable captured color target seed among 24 candidate(s): target payload is truncated
D3D12 real replay color readback: bytes=3686400 nonzero=3685797
```

The output remains the current incomplete BO2-derived title/logo replay. This is
expected: MP009 has the supported real textured draws, but its target snapshots
are the older bounded/truncated previews.

Validation on `target_full_mp_001` currently fails before native draw submission:

```text
D3D12 real replay unavailable: capture has no complete vertex/index snapshot that the current D3D12 real replay path can bind
```

That capture proves full target sidecars can be collected, but it was stopped
before the supported textured draw window. The next capture needs
`BO2_NATIVE_CAPTURE_FULL_TARGETS=1` plus a capture window that reaches the MP009
textured title/UI draws, while staying bounded enough not to fill the disk.

# Shader cache rebuild and MP010 replay - 2026-07-02

`CompileShaderWithCache` now treats an unreadable or zero-byte compiled shader
cache entry as rebuildable when HLSL source is available. If source is missing,
the backend still fails loudly. This fixed a real live-mode blocker where
`manual_vs_CBC9604F48930B36_vs_5_0_layout8_src2943490793FFE044.dxbc` existed as
a zero-byte file and prevented the CBC960 UI/background shader pairs from
creating a PSO.

Validation capture:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 64 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

Run result: watchdog-killed after 18 seconds by design. Capture size was about
`110 MB` including sidecars.

Offline D3D12 replay:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-d3d12.bmp --no-summary
```

Result:

```text
D3D12 real replay submitted 124 supported draw(s) across 7 shader pair(s)
  VS=0x261BDD733FEC1F64 PS=0xFF01D28E1EF3A880 submitted=18
  VS=0x3C4F6D40D699817B PS=0xEDC17DCC3FFDB040 submitted=47
  VS=0x5B9B7484417FB9B6 PS=0x3A6876055FEC1674 submitted=15
  VS=0xCBC9604F48930B36 PS=0x79E1F538A5074A65 submitted=3
  VS=0xCBC9604F48930B36 PS=0x7D1EF030F5710BDA submitted=18
  VS=0xCBC9604F48930B36 PS=0x8645E8BA65E424B2 submitted=9
  VS=0xEF95534343684F5B PS=0xDC168FB6031AFC41 submitted=14
captured texture SRVs=196
unsupported_texture_attempts=0
partial_texture_previews=0
diagnostic_pipelines=0
```

The output is now a recognizable BO2 menu/background/UI composition, but it is
still not correct final rendering. Colors and compositing are wrong, and A4 /
AB1E passes remain skipped until Xenos register/export/interpolator semantics
are decoded.

# Block-compressed texture endian fix - 2026-07-02

The green/purple MP010 background was traced to DXT/BC color endpoint endian
handling. The texture payloads were complete and the Xenos fetch state marked
them as endian mode `1`, but the BC decoder was reading RGB565 color endpoints
without applying the same endian correction used by other texture formats.

Evidence:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --draw 774 --dump-texture --texture-slot 0 --texture-output C:\Users\braxt\bo2-recompiled\native-renderer-mp010-draw774-texture0-bc-endian.bmp --no-summary
```

After applying endpoint endian correction, draw `774` texture slot `0`
decodes as:

```text
format=18 size=256x256 tiled=yes endian=1 payload=32768
avg_rgba=(145,145,145,255)
```

The full MP010 D3D12 replay now renders a plausible grayscale BO2 MP menu
background instead of the previous false green/purple image:

```powershell
native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-bc-endian.bmp --no-summary
```

Validation result:

```text
D3D12 real replay submitted 124 supported draw(s) across 7 shader pair(s)
captured texture SRVs=196
unsupported_texture_attempts=0
partial_texture_previews=0
diagnostic_pipelines=0
```

Remaining correctness blockers are now easier to see: A4/AB1E skipped passes,
missing exact shader ALU/export semantics, and live in-game parity with this
offline replay output.

# Depth-only A4 replay path - 2026-07-02

The no-texture `PS=0xA4A965C189287B99` class is still not treated as visible
scene color when its exported `r0` value cannot be proven. However, the
`VS=0x1E6883FCCDE1F688 / PS=0xA4A965C189287B99` subset in MP010 has complete
captured vertices and render state with depth/stencil side effects. The D3D12
real replay path now submits that subset with a forced zero color write mask,
so the real BO2 geometry can affect replay depth/stencil without reintroducing
the old false fullscreen/diagonal color output.

Validation command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-depthonly-a4.bmp --no-summary
```

Result:

```text
D3D12 real replay submitted 180 supported draw(s) across 8 shader pair(s)
  pair VS=0x1E6883FCCDE1F688 PS=0xA4A965C189287B99 submitted=56 captured=56
captured texture SRVs=196
unsupported_texture_attempts=0
partial_texture_previews=0
diagnostic_pipelines=0
forced_depth_only_zero_color_draws=56
```

Gap report after this change:

```text
draws=1314 geometry_ok=180 shader_ok=180 texture_ok=180 ready=180
utility_ready=0 depth_only_zero_color_ready=56 scene_candidate_ready=124
ignored_utility=1100
top blockers:
  23 x AB1E texture/interpolator semantics
  11 x A4 pass-through color export with no captured/proven color dependency
```

Color output:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-depthonly-a4.bmp`,
SHA-256 `4D11C18AFD29F24DFF62FBA97EB5FC07BD64ABAFF56FD41016EE8BEEFAE25BF6`.

Depth output:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-depthonly-a4-depth.bmp`,
SHA-256 `AF64BFC707E84C5C9B200056BC91AB6012794CB82D9B022DB47CCE3ACA480B7C`.

This is a correctness improvement for the real backend state path, not a claim
that A4 color semantics are solved. The remaining high-priority shader work is
still Xenos register/export/interpolator lowering for AB1E and the remaining
no-texture A4 draws.

# Offline D3D12 render-target cache - 2026-07-02

The offline D3D12 real replay path no longer collapses every captured guest
color base into one host render target. It now creates a replay-local render
target cache keyed by the captured guest color base and binds the matching RTV
per submitted draw. The final BMP readback uses the selected presented guest
base instead of whichever target happened to be bound last.

Validation command:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-rtcache.bmp --no-summary
```

Result:

```text
D3D12 real replay submitted 180 supported draw(s) across 8 shader pair(s)
D3D12 real replay PSO cache: entries=10 misses=10 hits=170 diagnostic_pipelines=0 cache_index_writes=10 input_layout_variants=3
D3D12 real replay bound 196 captured texture SRV(s), 1244 fallback texture SRV(s), unsupported_texture_attempts=0, partial_texture_previews=0
D3D12 real replay bound 196 captured sampler descriptor(s), 652 fallback sampler descriptor(s), exact_clamp_modes=196, clamp_addressing_fallbacks=652
D3D12 real replay offline render targets: count=3 presented_guest_color_base=0x530
D3D12 real replay color readback: bytes=3686400 nonzero=3685950 output=C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-rtcache.bmp
```

Output SHA-256:
`4D11C18AFD29F24DFF62FBA97EB5FC07BD64ABAFF56FD41016EE8BEEFAE25BF6`.

This does not complete render-target emulation. It is an offline replay
correctness fix for captures that use multiple guest color bases. Remaining RT
work: prove the presented guest base from swap/resolve metadata instead of the
current last-submitted-color-base heuristic, support multiple MRT slots, carry
real depth target contents instead of a synthetic replay DSV, and factor the
same target selection into live `native_d3d12`.

# Live D3D12 submit proof telemetry - 2026-07-02

The live `native_d3d12` path now carries real replay submission counters back
from the shared D3D12 replay backend into the live backend. Each
`live_d3d12_submit` capture event now records:

- `submitted_draws`
- `shader_pair_count`
- `pso_entries`
- `diagnostic_pipelines`
- `input_layout_variants`

The live backend also logs those counters after a successful native submit.
This does not by itself prove that every live BO2 frame is correct, but it gives
an immediate way to distinguish real native D3D12 draw submission from an empty
or diagnostic-only native window.

Build validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 default.exe"
```

Result: exit `0`, `263` Ninja steps, elapsed `214.7s`.

Replay regression after the live telemetry change:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_010\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp010-live-stats-regression.bmp --no-summary
```

Result:

```text
D3D12 real replay submitted 180 supported draw(s) across 8 shader pair(s)
D3D12 real replay PSO cache: entries=10 misses=10 hits=170 diagnostic_pipelines=0 cache_index_writes=10 input_layout_variants=3
D3D12 real replay offline render targets: count=3 presented_guest_color_base=0x530
```

Fresh live MP validation after rebuilding `default_mp.exe`:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Result: exit `0`, `8` Ninja steps, elapsed `21.9s`.

Live run command:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_011\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 64 --native_renderer_verbose true --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

The process was stopped after `120s`; the capture had already reached the
configured event limit and validates:

```text
Validation OK: 6500 events, 24 frames, 1290 draws
```

Aggregated `live_d3d12_submit` proof counters from the capture:

```text
live_submit_events=24
attempted=23
success=15
failed=8
total_submitted_draws=99
max_submitted_draws=18
max_shader_pairs=6
max_pso_entries=10
diagnostic_pipelines_sum=0
```

This proves strict live MP `native_d3d12` is now submitting real D3D12 replay
draws into the native backend for some frames, with no diagnostic pipelines.
It is still not complete live scene rendering because several frames fail
closed with `selected frame has no draw with complete geometry currently
supported by D3D12 real replay`, and many submitted frames still only cover a
subset of the captured frame.

Offline replay of the same fresh capture:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_011\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp011-replay.bmp --no-summary
```

Result:

```text
D3D12 real replay submitted 205 supported draw(s) across 8 shader pair(s)
D3D12 real replay PSO cache: entries=10 misses=10 hits=195 diagnostic_pipelines=0 cache_index_writes=10 input_layout_variants=3
D3D12 real replay offline render targets: count=3 presented_guest_color_base=0x530
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp011-replay.bmp`.

## 2026-07-02 live MP AB1E/A4 utility fill update

The D3D12 real replay path now accepts the narrow fullscreen utility class
`VS=0xAB1E86137A0240E8` / `PS=0xA4A965C189287B99` instead of treating it as
an unsafe zero-color export blocker. The acceptance gate is intentionally
specific: non-indexed rectangle-list draw, three vertices, no texture fetches,
captured render state present, color writes enabled, no depth write/stencil,
no captured `COLOR0`, ParamGen disabled in `SQ_PROGRAM_CNTL`, and decoded
positions covering the render target. Other A4 zero-export draws still fail
closed unless they are already proven depth/stencil-only writes.

Build validation after the change:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Both builds exited `0`.

Fresh strict live MP capture after the change:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_012\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 64 --native_renderer_verbose true --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

The process was stopped after `120s` once the capture was already populated.
Replay validation:

```text
Validation OK: 6500 events, 27 frames, 1366 draws
```

Aggregated live-submit counters from `live_d3d12_submit` events:

```text
live_submit_events=27
attempted=26
success=20
failed=6
total_submitted_draws=97
max_submitted_draws=11
max_shader_pairs=6
max_pso_entries=10
diagnostic_pipelines_sum=0
```

Offline real D3D12 replay of the same capture:

```powershell
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_012\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp012-ab1e-a4-fill.bmp --no-summary
```

Result:

```text
D3D12 real replay submitted 159 supported draw(s) across 9 shader pair(s)
pair VS=0xAB1E86137A0240E8 PS=0xA4A965C189287B99 submitted=9 captured=9
D3D12 real replay PSO cache: entries=12 misses=12 hits=147 diagnostic_pipelines=0 input_layout_variants=4
D3D12 real replay bound 156 captured texture SRV(s), 1116 fallback texture SRV(s), unsupported_texture_attempts=0
D3D12 real replay offline render targets: count=3 presented_guest_color_base=0x530
```

Output SHA-256:
`E5E2C7FA803D689B208A1A7137EB7A7A1F0B273D6E086615515E215CEB74CEDE`.

Current gap report for this capture:

```text
draws=1366 geometry_ok=159 shader_ok=159 texture_ok=159 ready=159 utility_ready=9 depth_only_zero_color_ready=58 scene_candidate_ready=92 ignored_utility=1207
top blockers: none
```

The rendered BMP shows a recognizable BO2 Multiplayer menu frame with the
background soldiers, logo/menu text, and button glyphs. This is real
BO2-derived offline D3D12 output, not diagnostic geometry. Remaining live
renderer work is still substantial: live `native_d3d12` only submits subsets of
some frames, frames with only ignored/no-output utility work still report as
failures, and exact live scene parity still needs resolved presentation,
complete texture/sampler coverage, and broader shader/state semantics.

## 2026-07-02 MP menu font mask and color-order fix

The MP012 offline output exposed a concrete UI correctness bug: menu text drew
in the right positions, but the glyphs appeared as solid white/blue rectangles.
The issue was not missing font texture capture. Draw `1363` uses real indexed
text quads with:

```text
VS=0x261BDD733FEC1F64 PS=0xFF01D28E1EF3A880
indices=192 indexed=yes
texture fetch: format=2, 512x1024, tiled=yes, payload=524288
vertex attr[1]: FMT_8_8_8_8 color, endian=2
```

The decoded font atlas preview contains real glyphs, but format `2` (`k_8`)
was being expanded to RGBA as `(r,r,r,1)`. The FF01 glyph override uses the
sampled max channel as coverage, so forced alpha `1` made every glyph quad
opaque. Format `2` now expands as `(r,r,r,r)`, preserving the mask in alpha.
The texture preview for draw `1363` changed from:

```text
avg_rgba=(86,86,86,255)
```

to:

```text
avg_rgba=(86,86,86,86)
```

The selected menu item also drew blue because `FMT_8_8_8_8` vertex colors with
fetch endian `2` were decoded through the generic swapped-word path. Raw bytes
`FF FF 66 00` are the BO2 selected text color in `A,R,G,B` byte order, which
should decode to orange `(1.0,0.4,0.0,1.0)`. The vertex decoder and replay
dump decoder now use byte order `R,G,B,A = byte1,byte2,byte3,byte0` for this
format/endian class. The same rule preserves the existing translucent white
quad case `1A FF FF FF` as `(1.0,1.0,1.0,0.101961)`.

Validation commands:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay"
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_012\events.jsonl --draw 1363 --dump-vertices --no-summary
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_012\events.jsonl --backend d3d12 --skip-unsupported --d3d12-output C:\Users\braxt\bo2-recompiled\native-renderer-live-mp012-format2-alpha-colororder.bmp --no-summary
```

Results:

```text
draw[1363] v[0] color=(1.000000,0.400000,0.000000,1.000000)
D3D12 real replay submitted 159 supported draw(s) across 9 shader pair(s)
D3D12 real replay PSO cache: entries=12 misses=12 hits=147 diagnostic_pipelines=0 input_layout_variants=4
D3D12 real replay bound 156 captured texture SRV(s), 1116 fallback texture SRV(s), unsupported_texture_attempts=0
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp012-format2-alpha-colororder.bmp`.

The image now shows readable BO2 menu glyphs and the selected `XBOX LIVE`
entry is orange. This is still offline replay evidence, not a claim that live
native rendering is complete.

## 2026-07-02 live no-op utility frame telemetry

Live `native_d3d12` now distinguishes true unsupported-frame failures from
frames whose selected command bucket contains only known ignored/no-output
utility draws. `RunD3D12LiveFrameBackend` returns success for that narrow
no-op case and records `noop_utility_frame=true` in `live_d3d12_submit`.
Frames with unsupported scene work, missing PSOs, or unsupported captured
textures still fail/skip through the existing strict paths.

Build validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay default.exe"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Both builds exited `0`. The default build reached `267` Ninja steps and the MP
build reached `6` steps.

Fresh live MP capture:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_013\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 64 --native_renderer_verbose true --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

The process was stopped after `120s` with capture data written. Validation:

```text
Validation OK: 6500 events, 23 frames, 1288 draws
```

Aggregated live-submit counters:

```text
live_submit_events=23
attempted=22
success=22
failed=0
noop_utility_frames=9
total_submitted_draws=119
max_submitted_draws=24
diagnostic_pipelines_sum=0
```

The no-op path removes false failed-frame accounting, but it also makes the
current live flicker easier to explain: live mode is still presenting a mix of
partial real frames and no-op utility-only frames instead of preserving or
compositing a stable last-good native frame. That is still live-renderer
incomplete work, not completion.

## 2026-07-02 live present gate and GPU stall reduction

Live `native_d3d12` no longer presents frames that did not submit any real
native draws. `live_d3d12_submit` now records `presented`, and the swapchain
present path is gated on `success && submitted_draws > 0`. Utility-only
no-op frames are still captured and counted, but they do not advance the
native D3D12 swapchain. This is intended to reduce the visible flicker caused
by alternating real partial frames with empty/stale utility buckets.

The live command-list path also no longer executes empty command lists and no
longer waits for the GPU immediately after every submitted command list. The
existing next-frame reset path and shutdown path still wait for GPU completion
before reusing command allocators/resources. This removes one full CPU/GPU
stall from every live frame.

Build validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay default.exe"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Both builds exited `0`.

Fresh live MP capture after the present gate:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_015\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 64 --native_renderer_verbose true --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

The process stayed alive until the `120s` guard stopped it. Validation:

```text
Validation OK: 6500 events, 24 frames, 1299 draws
```

Aggregated live-submit counters:

```text
live_submit_events=23
attempted=23
success=23
failed=0
noop_utility_frames=9
presented=14
not_presented=9
total_submitted_draws=111
max_submitted_draws=19
diagnostic_pipelines_sum=0
```

Fresh offline replay of the preceding capture (`live_d3d12_mp_014`) still
produced coherent BO2 MP menu output:

```text
D3D12 real replay submitted 232 supported draw(s) across 8 shader pair(s)
D3D12 real replay PSO cache: entries=11 misses=11 hits=221 diagnostic_pipelines=0
D3D12 real replay bound 282 captured texture SRV(s), 1574 fallback texture SRV(s)
D3D12 real replay bound 282 captured sampler descriptor(s), 934 fallback sampler descriptor(s)
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp014-present-gate-regression.bmp`.

Remaining issues visible from these counters:

* Live is still partial: only a subset of the captured draw stream reaches the
  native backend each frame.
* Texture quality can still be wrong because all uploaded D3D12 texture SRVs
  are single-mip `R8G8B8A8_UNORM` previews and many unused/fallback slots are
  still bound per draw.
* The renderer still needs real mip payload upload, better sampler LOD
  handling, and fewer fallback descriptors before live output can be called
  complete.

## 2026-07-02 live accumulated color target

User-observed live failure after the present gate: different parts of the
same image flickered independently. Examples included the background going
black while text remained visible, or the top-right version text disappearing
while other UI stayed visible. This showed that live `native_d3d12` was still
presenting partial render buckets directly into DXGI flip-discard backbuffers.
Because a flip-discard backbuffer does not preserve previous contents, any
bucket missing the background/UI subpass could present black or stale regions.

The live D3D12 replay path now owns a persistent session color target:

* successful native draw batches execute even if they are not presentable
* draw batches render into the persistent accumulated target, not directly
  into the swapchain backbuffer
* non-presentable partial buckets are not copied to the swapchain
* presentable buckets copy the accumulated target to the current swapchain
  backbuffer and then present
* `live_d3d12_submit` now records draw classes:
  `scene_candidate_draws`, `depth_only_draws`, `utility_draws`,
  and `presentable_frame`

This is still a partial live renderer. It is a stability fix for partial
frame flicker, not a claim that the native renderer is complete.

Build validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay default.exe"
```

Both builds exited `0`.

Fresh live MP capture:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_019\events.jsonl --native_renderer_capture_limit 6500 --native_renderer_capture_flush_interval 64 --native_renderer_verbose true --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

The process stayed alive until the `120s` guard stopped it. Validation:

```text
Validation OK: 6500 events, 25 frames, 1317 draws
```

Aggregated live-submit counters:

```text
live_submit_events=24
attempted=23
success=23
failed=0
noop_utility_frames=11
presentable=6
presented=6
not_presented=18
total_submitted_draws=111
total_scene_draws=73
total_depth_draws=30
total_utility_draws=8
diagnostic_pipelines_sum=0
```

Offline replay of the same capture:

```text
D3D12 real replay submitted 232 supported draw(s) across 8 shader pair(s)
D3D12 real replay bound 282 captured texture SRV(s), 1574 fallback texture SRV(s)
D3D12 real replay draw classes: scene_candidate=159 depth_only=58 utility=15
D3D12 real replay color readback: bytes=3686400 nonzero=3685950
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp019-accumulation-regression.bmp`.

Remaining live issues:

* The live renderer still submits only a subset of the game command stream per
  bucket, so a true full-frame scheduler/compositor is still needed.
* Texture quality/weirdness remains unresolved. The generated mip experiment
  was not kept because it did not address the reported issue and would add
  CPU work to live replay.
* The native renderer still relies on many fallback texture/sampler slots, and
  several shader/material semantics are still manually approximated.

## 2026-07-02 720p and sampler-addressing pass

User-observed issues after the accumulated target change:

* the native live window still flickered between partial frame contents
* some text appeared as white squares
* textures looked pixelated/weird
* the user expected the current target to be 720p

Resolution evidence:

* Captured Xenos render state for the MP menu uses `window_scissor=0,0 ->
  1280,720`, viewport center/extent matching `1280x720`, and
  `surface_pitch=1280`.
* The native D3D12 live backend already defaults `frame_width_=1280` and
  `frame_height_=720`, and creates its DXGI swapchain from that frame size or
  from captured command-buffer snapshot dimensions.
* The ReXGlue app defaults were still forcing `video_mode_width=854` and
  `video_mode_height=480` for both SP and MP launchers. Those defaults now use
  `1280x720` so the host app window matches the native/captured 720p target.

Texture evidence:

* Draw 28 format-19 `2048x128` texture decoded cleanly as the repeated Treyarch
  icon strip.
* Draw 29 format-20 `256x64` texture decoded cleanly as the BO2 logo.
* Draw 511 format-6 `1024x1024` texture decoded cleanly as a dark hex-pattern
  background.
* Draw 513 format-2 `512x1024` texture decoded cleanly as the font atlas.

That evidence points away from base tiled texture decode for those menu
resources. The immediate bug found in the manual D3D12 shader overrides was
shader-side UV clamping: several overrides sampled `saturate(input.uv)`,
which bypassed captured Xenos sampler address modes. Effect draws in this
capture include wrap samplers (`clamp=0,0,0`), so forcing shader-side clamp can
produce smeared/weird imagery. The overrides now pass raw UVs/offset UVs to the
D3D12 sampler and let the captured sampler state choose wrap or clamp.

The font override also now outputs straight-alpha text color:

```text
rgb = input color
alpha = input alpha * sampled font coverage
```

instead of premultiplying RGB and then using the captured straight-alpha blend
state.

Build validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay default.exe"
```

The MP target was up to date. The debug/default target completed after the
guard timeout expired; a follow-up process check confirmed the existing Ninja
process completed.

Replay validation:

```text
Validation OK: 6500 events, 25 frames, 1317 draws
```

Post-fix offline replay of `live_d3d12_mp_019`:

```text
D3D12 real replay submitted 232 supported draw(s) across 8 shader pair(s)
D3D12 real replay PSO cache: entries=11 misses=11 hits=221 diagnostic_pipelines=0
D3D12 real replay bound 282 captured texture SRV(s), 1574 fallback texture SRV(s)
D3D12 real replay bound 282 captured sampler descriptor(s), 934 fallback sampler descriptor(s)
D3D12 real replay draw classes: scene_candidate=159 depth_only=58 utility=15
D3D12 real replay color readback: bytes=3686400 nonzero=3685950
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-mp019-resolution-sampler-fix.bmp`.

Fresh visible live MP run:

```powershell
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_capture_path C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_020\events.jsonl --native_renderer_capture_limit 7500 --native_renderer_capture_flush_interval 64 --native_renderer_verbose true --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

The process stayed alive until the `75s` guard stopped it. Validation:

```text
Validation OK: 7500 events, 26 frames, 1458 draws
```

Aggregated live-submit counters:

```text
live_submit_events=26
attempted=25
success=25
failed=0
noop_utility_frames=10
presentable=7
presented=7
not_presented=19
total_submitted_draws=152
total_scene_draws=107
total_depth_draws=36
total_utility_draws=9
max_scene_draws=25
diagnostic_pipelines_sum=0
```

Follow-up target-selection fix: whole-capture offline replay of MP020 initially
selected a late depth/utility-like guest color base (`0x260`) and read back
black (`nonzero=0`). The D3D12 replay target chooser now scores submitted draw
color bases and prefers the dominant scene-candidate target while excluding
depth-base matches when a better color target exists.

Post-fix MP020 replay:

```text
D3D12 real replay submitted 278 supported draw(s) across 9 shader pair(s)
D3D12 real replay draw classes: scene_candidate=199 depth_only=63 utility=16
D3D12 real replay offline render targets: count=3 presented_guest_color_base=0x530
D3D12 real replay color readback: bytes=3686400 nonzero=3685950
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-mp020-target-select-fix.bmp`.

Remaining live issues:

* The native renderer is still not complete. It still supports only a subset of
  the captured shader pairs and resource/state semantics.
* Live present scheduling still receives presentable buckets interleaved with
  utility/noop buckets. The live D3D12 path now suppresses per-frame replay
  stdout and re-presents the retained accumulated native frame for noop utility
  buckets after a real frame has been rendered. This reduces black flip-discard
  gaps, but it is still not a full command-stream scheduler.
* Several shader effects are still manual approximations, especially the
  multi-texture background/effect pairs.

## 2026-07-02 Live retained-frame present update

Resolution status:

* The current host window and native render target are `1280x720`.
* SP and MP launcher defaults set `video_mode_width=1280` and
  `video_mode_height=720`.
* D3D12 live/replay fallback dimensions are also `1280x720`.
* Captured BO2 render state confirms the same target:
  `surface_pitch=1280` and window scissor `0,0 -> 1280,720`.

Change:

* `RunD3D12RealReplayBackend` no longer prints the full D3D12 replay summary
  for every live frame submission. Offline replay output is unchanged.
* `D3D12LiveSubmitBinding` now reports `copied_retained_frame`.
* Live frames with any scene-candidate draw are now marked presentable. The old
  `scene_candidate_draws >= 6` threshold skipped real low-draw scene buckets,
  which showed up as avoidable non-presented live frames.
* For live noop utility buckets, the D3D12 replay backend copies the retained
  accumulated native render target to the current swapchain backbuffer when a
  retained target is available. The live backend executes and presents that
  copy, while still recording whether the bucket was truly presentable.
* `live_d3d12_submit` JSONL events now include `copied_retained_frame`.

Build validation:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay default.exe"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

The debug/default build and MP RelWithDebInfo build both completed. The MP
incremental rebuild took about 10 seconds for the final schema update.

MP021 pre-retained-copy evidence:

```text
exit=-1073741795 (0xC000001D)
stdout=0 bytes
stderr=0 bytes
Validation OK: 7500 events, 26 frames, 1454 draws
live_submit_events=26 attempted=25 success=25 failed=0
presentable=6 presented=6 not_presented=20
diagnostic_pipelines_sum=0
```

MP022 retained-copy evidence:

```text
exit=timeout_killed after 75s guard
stdout=0 bytes
stderr=0 bytes
Validation OK: 7500 events, 27 frames, 1489 draws
live_submit_events=27 attempted=26 success=26 failed=0
presentable=7 presented=18 not_presented=9
diagnostic_pipelines_sum=0
```

MP023 copied-retained telemetry:

```text
exit=timeout_killed after 45s guard
stdout=0 bytes
stderr=0 bytes
Validation OK: 5000 events, 21 frames, 1043 draws
live_submit_events=20 attempted=20 success=20 failed=0
noop_utility_frames=6
presentable=3
copied_retained=5
presented=8
diagnostic_pipelines_sum=0
```

Fresh MP023 offline replay:

```text
D3D12 real replay submitted 131 supported draw(s) across 9 shader pair(s)
D3D12 real replay PSO cache: entries=12 misses=12 hits=119 diagnostic_pipelines=0
D3D12 real replay bound 142 captured texture SRV(s), 906 fallback texture SRV(s)
D3D12 real replay draw classes: scene_candidate=78 depth_only=44 utility=9
D3D12 real replay color readback: bytes=3686400 nonzero=3685914
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-mp023-retained-present.bmp`.

MP024 low-scene present threshold update:

The previous live presentability threshold required at least six
scene-candidate draws. MP023 showed several real low-draw scene buckets with
`scene_candidate_draws=2` that were submitted but not presented. The live
threshold now marks any bucket with at least one scene-candidate draw as
presentable; noop utility buckets still use retained-frame copy when possible.

```text
exit=timeout_killed after 45s guard
stdout=0 bytes
stderr=0 bytes
Validation OK: 5000 events, 19 frames, 988 draws
live_submit_events=19 attempted=19 success=19 failed=0
noop_utility_frames=8
presentable=9
copied_retained=7
presented=16
not_presented=3
diagnostic_pipelines_sum=0
```

Compared with MP023, presented live-submit events improved from `8/20` to
`16/19`. The remaining non-presented events are startup buckets with no
scene-candidate draw and no retained native frame yet.

Fresh MP024 offline replay:

```text
D3D12 real replay submitted 163 supported draw(s) across 9 shader pair(s)
D3D12 real replay PSO cache: entries=12 misses=12 hits=151 diagnostic_pipelines=0
D3D12 real replay bound 180 captured texture SRV(s), 1124 fallback texture SRV(s)
D3D12 real replay draw classes: scene_candidate=108 depth_only=44 utility=11
D3D12 real replay color readback: bytes=3686400 nonzero=3686400
```

Output:
`C:\Users\braxt\bo2-recompiled\native-renderer-mp024-low-scene-present.bmp`.

Texture sampler LOD clamp:

The D3D12 replay/live resource path still uploads captured textures as a single
base mip (`MipLevels=1`). Several BO2 texture fetch records carry nonzero
Xenos mip filter/range state, so allowing an unbounded native D3D12 sampler LOD
can sample in ways that do not match the currently materialized resource. The
sampler now clamps `MaxLOD` to `0.0f` until packed mip capture/decode/upload is
implemented. This is not a complete texture solution; it is a guardrail for the
current base-mip-only renderer path while the remaining texture work continues.

MP028/MP029 live stability update:

Two stability issues were separated:

- The native D3D12 render window was created on the game thread, so long
  synchronous replay/capture work could starve the window message pump and make
  Windows mark the native output window as Not Responding. The owned native
  D3D12 window now runs on a small host window thread with its own message loop.
- The machine hit `no space on device` while flushing `default_mp` logs. That
  made capture/log/crash evidence unreliable until generated renderer artifacts
  were cleaned up.

After the window-thread change, a captured MP028 run stayed responsive until
around the capture/log pressure point and produced a valid native D3D12 menu
frame:

```text
Validation OK: 5000 events, 20 frames, 1022 draws
D3D12 real backend gap report:
  draws=1022 geometry_ok=165 shader_ok=165 texture_ok=165 ready=165
  utility_ready=12 depth_only_zero_color_ready=46
  scene_candidate_ready=107 ignored_utility=857
```

Output evidence:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp028-screen-12s.bmp`.

With capture disabled, MP029 stayed `Responding=True` for the full 45 second
watchdog window and was killed intentionally by the guard. This indicates the
window-thread patch fixes the primary Windows Not Responding behavior, while
capture/log volume and remaining live present/retained-frame behavior still need
more work.

Remaining visual issues:

- Some live frames still present black/stale output even though replay telemetry
  reports retained-frame copies on noop utility buckets.
- Texture quality is still incomplete. The sampler LOD clamp only prevents
  base-mip-only resources from sampling nonexistent native mips; it does not
  implement packed mip upload.
- Animated/atlas textures can appear as the whole texture stretched into the
  draw. That points at missing Xenos shader/constant UV transform semantics and
  atlas-frame selection, not a 720p/1080p resolution problem.

MP032/MP033 persistent live state update:

The live D3D12 frame builder now treats shaders and constants as persistent GPU
register state instead of clearing them at every swap. BO2 frequently uploads
only changed ranges, so per-swap clearing could drop atlas/animation constants
and make native frames alternate between valid UI/background output and missing
or black elements. Pending out-of-frame shader/constant work is merged into the
next frame and then the pending bucket is hard-reset so stale pending draws are
not replayed repeatedly.

Validation:

```text
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Result: exit `0`, linked `default_mp.exe`.

No-capture live run:

```text
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

Result: `Responding=True` at every 3 second sample for 25 seconds; the process
was killed by the watchdog, not by a native-renderer crash. Visual sequence
captures at 8s, 13s, and 18s all showed the BO2 MP menu in the native D3D12
window. The 8s and 13s BMPs had identical SHA-256
`E3ABD883FFCBF11DED80BE4903ECD8F21EB400C9209DFE75A3B0E17CEE9C4497`; the 18s
BMP had SHA-256 `24BC12C4497C338A15D3DCF2043CCC012FC080714EFF67D31A8B7BBB30107408`
and still showed a valid menu frame rather than a black frame.

Current output evidence:
`C:\Users\braxt\bo2-recompiled\native-renderer-live-mp032-persistent-state-12s.bmp`
and `C:\Users\braxt\bo2-recompiled\native-renderer-live-mp033-seq-18s.bmp`.

Remaining texture blocker after this fix:

The sampled MP028 capture proves the affected textured shaders are still missing
some constant ranges in native capture/replay. For example, runtime pixel shader
`0x8645E8BA65E424B2` reads `c72`, `c73`, `c232-c235`, and `c252-c255`, but the
captured PM4 constants around menu/effect draws only include ranges beginning at
indices `1008`, `1904`, `1952`, and `2032`. Persistent live state fixes dropped
per-frame constants, but it does not invent ranges the capture never observes.
The next texture-specific task is to trace the XEX constant emitters and
ReXGlue CP constant events until those missing ranges are captured or their
initial/static source is identified.

MP034 atlas constant-slot update:

The MP028 capture shows the active atlas/animated texture pixel shader draws
bind captured PM4 float constant ranges at indices `1952` and `2032`. The D3D12
replay backend materializes those as sparse float4 slots using `index >> 3`, so
they become slots `244` and `254`. Two manual pixel shader overrides were still
reading raw/guessed slots such as `captured_constants[2032 & 511]` and
`captured_constants[232]`, which left atlas/UV transform inputs zero or
unrelated. That maps directly to the symptom where an animated texture sheet is
sampled as a stretched whole texture instead of the intended frame.

Validation:

```text
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\live_d3d12_mp_028\events.jsonl --backend d3d12 --d3d12-output native-renderer-mp034-atlas-constant-fix-replay.bmp --shader-override-root shader_work\native_overrides --skip-unsupported --d3d12-draws 1200
```

Result:

```text
D3D12 real replay submitted 165 supported draw(s) across 8 shader pair(s)
D3D12 real replay PSO cache: entries=11 misses=11 hits=154 diagnostic_pipelines=0
D3D12 real replay bound 204 captured texture SRV(s), 1116 fallback texture SRV(s)
D3D12 real replay color readback: bytes=3686400 nonzero=3686398 output=native-renderer-mp034-atlas-constant-fix-replay.bmp
```

Output evidence:
`C:\Users\braxt\bo2-recompiled\native-renderer-mp034-atlas-constant-fix-replay.bmp`.

Remaining texture work:

- Replace the manual atlas approximations with real lowered Xenos ALU/texture
  coordinate code for shaders `0x7D1EF030F5710BDA` and
  `0x8645E8BA65E424B2`.
- Capture or reconstruct the missing static/initial constant ranges reported by
  `scripts/windows/audit_native_constant_coverage.ps1`.
- Implement packed mip capture/upload instead of relying on the temporary base
  mip sampler clamp.

MP038 semantic constant-gap diagnostics:

`D3D12ReplayBackend.cpp` now tracks which sparse captured constant slots are
actually present when building the native constant buffer, and D3D12 real replay
prints semantic gaps for the two active MP atlas/animated-texture pixel shaders.
This is diagnostic-only; it does not add another shader approximation.

Validation:

```text
default\out\build\win-amd64-clangmsvc-debug\native_render_replay.exe --capture native_captures\live_d3d12_mp_028\events.jsonl --backend d3d12 --d3d12-output native-renderer-mp038-semantic-constant-gaps.bmp --shader-override-root shader_work\native_overrides --skip-unsupported --d3d12-draws 1200
```

Result:

```text
D3D12 real replay submitted 165 supported draw(s) across 8 shader pair(s)
D3D12 real replay bound 204 captured texture SRV(s), 1116 fallback texture SRV(s)
D3D12 real replay semantic constant gaps:
  PS=0x7D1EF030F5710BDA missing=c72,c235 draws=6 example_draw=877 present_slots=c126,c127,c128,c129,c238,c239,c240,c241,c242,c243,c244,c245,c246,c247,c248,c249,c250,c251,c252,c253,c254,c255,c256,c257
  PS=0x7D1EF030F5710BDA missing=c72,c235,c252,c253 draws=14 example_draw=455 present_slots=c126,c127,c128,c129,c244,c245,c246,c247,c254,c255,c256,c257
  PS=0x8645E8BA65E424B2 missing=c72,c73,c232,c233,c234,c235 draws=3 example_draw=876 present_slots=c126,c127,c128,c129,c238,c239,c240,c241,c242,c243,c244,c245,c246,c247,c248,c249,c250,c251,c252,c253,c254,c255,c256,c257
  PS=0x8645E8BA65E424B2 missing=c72,c73,c232,c233,c234,c235,c252,c253 draws=7 example_draw=454 present_slots=c126,c127,c128,c129,c244,c245,c246,c247,c254,c255,c256,c257
D3D12 real replay color readback: bytes=3686400 nonzero=3686398 output=native-renderer-mp038-semantic-constant-gaps.bmp
```

This confirms the animated/atlas texture distortion is not caused by missing
texture payloads in MP028. The capture has the texture sidecars, but the current
native constant state lacks several constants used by the real Xenos texture
coordinate ALU. Example `draw[454]` uses `PS=0x8645E8BA65E424B2`, whose semantic
IR requires `c72`, `c73`, `c232-c235`, and `c252-c255`; replay only has
`c126-c129`, `c244-c247`, and `c254-c257` present for that draw.

The failed frame-ring pacing attempt was also backed out before commit. The live
path currently renders into one persistent accumulation target and one persistent
depth target, so a command-list ring without per-frame render/depth targets and
fenced resource retention can race/release GPU resources. The stable serialized
path remains active until that lifetime model is implemented.

MP036 Phase 0 diagnostics update:

The live D3D12 path now exposes low-noise per-run diagnostics for frame and draw
drop behavior. `D3D12LiveSubmitBinding` carries the replay planner's skipped
draw count, noop-elided draw count, and grouped unsupported reasons back to the
live backend. `D3D12LiveRendererBackend` aggregates:

- attempted/submitted native frames
- frames that were presentable or used a retained copy
- non-presentable frames
- retained-frame copies
- skipped draws
- noop-elided draws
- diagnostic pipeline usage
- top unsupported draw reasons

Validation:

```text
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Result: exit `0`.

```text
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 default native_render_replay"
```

Result: completed and linked `default.exe` plus `native_render_replay.exe`.

Bounded live run:

```text
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

Result: `Responding=True` at every 3 second sample through 24 seconds; process
was killed intentionally by the watchdog. The log
`default_mp/out/build/win-clang-msvc-amd64-relwithdebinfo-msvctarget/logs/default_mp_045.log`
contains diagnostics such as:

```text
BO2 native D3D12 diagnostics frames_attempted=7 submitted=7 presented_or_retained=4 not_presentable=3 retained_copies=1 skipped_draws=0 elided_noop_draws=148 diagnostic_pipelines=0 top_unsupported=[none]
```

This proves the currently visible flicker/drop pattern is not from diagnostic
shader fallback in this run (`diagnostic_pipelines=0`). The measured live issue
is alternating presentable scene frames with noop/retained-copy frames and some
depth-only/non-presentable native submissions. Phase 1 should address the
remaining frame pacing/full-stall problem, while Phase 2 still needs the
ReXGlue translator integration for shader-complete rendering.

Replay validation:

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_028\events.jsonl --validate --no-summary
```

Result: `Validation OK: 5000 events, 20 frames, 1022 draws`.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_028\events.jsonl --backend d3d12 --d3d12-output native-renderer-mp036-diagnostics-replay.bmp --shader-override-root shader_work\native_overrides --skip-unsupported --d3d12-draws 512
```

Result: `165` supported real D3D12 draws, `0` diagnostic pipelines,
`204` captured texture SRVs, and nonzero color/depth readbacks.

MP043 live retained-present stability update:

The live D3D12 backend now avoids copying retained color before a real
presentable color frame has been produced. Once a presentable frame exists,
noop/utility frames can copy the persistent accumulated color target into the
current swapchain buffer and mark `copied_retained_frame=yes`. This is intended
to reduce the visible black-frame/dropout pattern without presenting an
uninitialized early accumulation target.

The native D3D12 render window thread now pumps its own message queue with a
bounded wait instead of blocking indefinitely in `GetMessageW`. The present path
also logs failed `IDXGISwapChain::Present` calls with the failing HRESULT.

Validation:

```text
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Result: exit `0`.

```text
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default\out\build\win-amd64-clangmsvc-debug"" -j12 native_render_replay.exe"
```

Result: exit `0`.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_028\events.jsonl --validate --no-summary
```

Result: `Validation OK: 5000 events, 20 frames, 1022 draws`.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_028\events.jsonl --backend d3d12 --d3d12-output native-renderer-mp042-retained-gated.bmp --shader-override-root shader_work\native_overrides --skip-unsupported --d3d12-draws 1200
```

Result: `165` supported real D3D12 draws across `8` shader pairs, `0`
diagnostic pipelines, `204` captured texture SRVs, and nonzero color/depth
readbacks.

Bounded live run:

```text
default_mp.exe --native_renderer_mode native_d3d12 --native_renderer_verbose false --native_renderer_live_allow_diagnostic_shader false --native_renderer_skip_unsupported_draws true
```

Result: responsive at every sample through `24` seconds; process was killed by
the watchdog. Latest log:
`default_mp/out/build/win-clang-msvc-amd64-relwithdebinfo-msvctarget/logs/default_mp_053.log`.

Relevant log evidence:

```text
BO2 native D3D12 live frame 6 submitted ... presentable_frame=no noop_utility_frame=yes copied_retained_frame=yes
BO2 native D3D12 live frame 65 submitted real_draws=26 shader_pairs=7 ... presentable_frame=yes ... diagnostic_pipelines=0
```

No `Present failed` lines were emitted in the run. Remaining live issues are
still real renderer gaps: some frames have no currently supported complete
geometry, shader constants for atlas/animated texture coordinate selection are
missing from native state, and the live path is still serialized around GPU
fences rather than using a proper frame/resource ring.

MP049 draw-probe update:

The MP generated-function detour scanner now handles the shorter RelWithDebInfo
prologues used by the generated PPC bodies. This allows direct generated-body
hooks to install for the current MP draw packet helpers instead of relying only
on dispatcher replacement.

Shader-record probe mode now also records bounded snapshots for MP draw packet
helpers. The first-level draw argument block comes from `r5`, and the probe
follows the nested pointer at dword `16` when present. This captures the live
draw-side state needed to continue mapping material/pass constants used by
atlas and animated texture coordinate shaders.

Validation:

```text
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ninja -C ""C:\Users\braxt\bo2-recompiled\default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget"" -j12 default_mp.exe"
```

Result: exit `0`.

```text
default_mp.exe --native_renderer_mode=native_d3d12 --native_renderer_capture_path=C:\Users\braxt\bo2-recompiled\native_captures\live_d3d12_mp_049_draw_probe_nested\events.jsonl --native_renderer_capture_limit=9000 --native_renderer_capture_flush_interval=64 --native_renderer_shader_record_probe_mode=on --native_renderer_verbose=false --native_renderer_live_allow_diagnostic_shader=false --native_renderer_skip_unsupported_draws=true
```

Result: responsive through `15` seconds; watchdog stopped the process after the
capture reached its event cap.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_049_draw_probe_nested\events.jsonl --validate --no-summary
```

Result: `Validation OK: 9000 events, 30 frames, 1722 draws`.

Probe evidence:

```text
shader_record_probe: 34
probe[0] sub_82117BC8 r5=0x7026FCB0 primary=0x7026FCB0 secondary=0x7026FD60
```

The secondary block contains repeated runtime/XEX pointers including
`0x827C0C98`, `0x827C11E4`, `0x84766B00`, `0x848D0000`, and `0x8490B81C`.
The currently active Ghidra program did not resolve the MP addresses, so the
next reverse-engineering step is to switch Ghidra to the MP/XEX program and map
those fields to material/pass or draw-state structures.

MP050-MP053 draw-probe follow-up:

Shader-record probes now support deeper bounded snapshots:

* `primary_*`: the first draw argument/state block.
* `secondary_*`: the nested block referenced by the draw argument block.
* `tertiary_*` and `quaternary_*`: stack/material staging links followed from
  the secondary block.
* `heap_candidate_*`: the first non-empty runtime heap record referenced by the
  captured probe chain.

Validation:

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_050_draw_probe_tertiary\events.jsonl --validate --no-summary
```

Result: `Validation OK: 9000 events, 30 frames, 1750 draws`, with `17`
shader-record probes and `15` tertiary snapshots.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_051_draw_probe_quaternary\events.jsonl --validate --no-summary
```

Result: `Validation OK: 9000 events, 30 frames, 1729 draws`, with `28`
shader-record probes and `25` quaternary snapshots.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_052_draw_probe_heap_candidate\events.jsonl --validate --no-summary
```

Result: `Validation OK: 9000 events, 30 frames, 1729 draws`, with `28`
shader-record probes and `25` heap-candidate snapshots.

Evidence:

```text
probe[0] sub_82117BC8 primary=0x7026FCB0 secondary=0x7026FD60
tertiary=0x7026FE00 quaternary=0x7026FF10 heap_candidate=0x84766B00
```

The tertiary/quaternary chain is mostly stack/staging data (`0xBEBEBEBE`
sentinels and stack pointers), not the missing atlas constants. Ghidra MCP
confirmed `default.xex` is loaded for XEX addresses, but the `0x847...` and
`0x848...` values captured here are runtime heap addresses outside the static
XEX image. The probe now skips empty heap candidates when possible so later
captures can select a meaningful material/state record.

The latest MP053 capture reached the event cap with no draw-probe events before
the cap, even though the generated-function detours installed successfully. This
is capture timing/path variability, not evidence that the hook code is absent.
The renderer freeze remained reproducible in that run: the process was not
responding when the watchdog terminated it, while the capture itself still
validated (`9000` events, `32` frames, `1800` draws). The current live-stability
target is therefore still the D3D12 live frame/resource synchronization path.

Live D3D12 retained-frame fast path:

The live D3D12 backend now classifies frame-plan draws before uploading D3D12
resources. In live mode only, frames with no scene-color candidate skip the
expensive resource build/upload path and copy the retained color target when a
previous presentable frame exists. Offline replay is unchanged.

Validation:

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_052_draw_probe_heap_candidate\events.jsonl --backend d3d12 --d3d12-output native-renderer-mp052-live-fastpath-replay.bmp --shader-override-root shader_work\native_overrides --skip-unsupported --d3d12-draws 1200
```

Result: unchanged offline behavior, `354` supported real draws across `9`
shader pairs, nonzero color/depth output, and the same semantic constant gaps
for the atlas/animated shaders.

Bounded live validation:

```text
default_mp.exe --native_renderer_mode=native_d3d12 --native_renderer_verbose=false --native_renderer_live_allow_diagnostic_shader=false --native_renderer_skip_unsupported_draws=true
```

Result: responsive at every 4 second sample through 28 seconds; watchdog stopped
the process. Latest log:
`default_mp/out/build/win-clang-msvc-amd64-relwithdebinfo-msvctarget/logs/default_mp_064.log`.

Relevant evidence:

```text
frame 2: real_draws=0 scene_draws=0 depth_only_draws=1 copied_retained_frame=no
frame 4: real_draws=2 scene_draws=2 presentable_frame=yes
frame 5: real_draws=0 scene_draws=0 copied_retained_frame=yes
frame 7: real_draws=0 scene_draws=0 copied_retained_frame=yes
```

No `Present failed` or fence wait errors were emitted in the validation run.
This reduces wasted live work on depth-only/utility-only frames and should
reduce black-frame flicker and frame-time spikes, but it does not fix the
remaining texture/atlas correctness issue. The texture issue still tracks to
missing semantic shader constants (`c72`, `c73`, `c232-c235`, and sometimes
`c252-c253`) rather than mip selection; base texture payloads are present and
the D3D12 sampler is clamped to mip 0.

Draw-time float constant snapshots:

ReXGlue now copies the full Xenos ALU float constant file (`c0..c511`, 2048
dwords) into each native draw trace event. The BO2 capture writer stores this
snapshot as `resources/float_constants_*.bin` sidecars instead of inline JSON
so captures do not produce giant draw lines. Replay loads those sidecars and
repacks the raw Xenos register-file layout (`cN` at raw dword `N * 8`) into
the native D3D12/HLSL `float4 captured_constants[]` layout before applying
explicit PM4 constant uploads.

The MP menu pixel shader overrides for `PS=0x7D1EF030F5710BDA` and
`PS=0x8645E8BA65E424B2` now consume that repacked buffer by semantic constant
index (`c72`, `c73`, `c232-c235`, `c252-c255`). Older override code still used
stale raw/register-file offset math such as `1952 >> 3` for atlas constants;
that selected `c244` instead of `c232` after the full constant snapshot path
landed. The overrides now preserve wrap/animated atlas coordinates more closely
to the decoded Xenos ALU and leave captured sampler address modes in control.

The D3D12 sampler filter mapping also treats Xenos texture filter value `3` as
`kUseFetchConst` instead of nearest-point. Native replay does not yet carry a
separate shader sampler binding for every manual override, so unresolved
`kUseFetchConst` now falls back to linear filtering for mag/min/mip sampling.
This avoids turning BO2 UI/menu atlas samples into visibly blocky nearest-point
samples while the full sampler binding translator is still incomplete.

Validation:

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_057_float_constants_sidecar\events.jsonl --validate --no-summary
```

Result: `Validation OK: 4000 events, 17 frames, 828 draws`.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_057_float_constants_sidecar\events.jsonl --backend d3d12 --d3d12-output native-renderer-mp057-float-constants-replay.bmp --shader-override-root shader_work\native_overrides --skip-unsupported --d3d12-draws 1200
```

Result: `118` supported real draws across `8` shader pairs, nonzero
color/depth output, `148` captured texture SRVs, and no semantic constant gap
report for `PS=0x7D1EF030F5710BDA` or `PS=0x8645E8BA65E424B2`. This removes
the proven missing-constant cause of stretched/static atlas sampling. Remaining
texture issues should now be debugged as shader math/translation, sampler
addressing, or texture decode issues rather than absent `c72/c235/...` state.

Texture upload cache and bounded live probe:

The real D3D12 replay/live submit path now caches per-submit texture uploads by
captured texture identity (`base`, `mip`, dimensions, pitch, format, endian,
tiled flag, payload size/resource path) and uses one shared white RGBA8 fallback
texture for fallback slots. SRV and sampler descriptors are still emitted per
draw/slot, so shader binding behavior is unchanged; only duplicate D3D12
resource/upload creation is avoided. This is a performance/resource-churn fix,
not a shader correctness fix.

Validation:

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_057_float_constants_sidecar\events.jsonl --validate --no-summary
```

Result: `Validation OK: 4000 events, 17 frames, 828 draws`.

```text
native_render_replay.exe --capture native_captures\live_d3d12_mp_057_float_constants_sidecar\events.jsonl --backend d3d12 --d3d12-output native-renderer-mp057-texture-cache-replay.bmp --shader-override-root shader_work\native_overrides --skip-unsupported --d3d12-draws 1200
```

Result: `118` supported real draws across `8` shader pairs,
`148` captured texture SRVs, `796` fallback texture SRVs, and:

```text
D3D12 real replay texture upload cache: entries=149 misses=149 hits=795
```

The replay output remains a partially correct MP menu/background image. It is
still not a finished renderer: UI/text correctness and broader shader/texture
semantics remain incomplete.

Bounded live validation:

```text
default_mp.exe --native_renderer_mode=native_d3d12 --native_renderer_verbose=false --native_renderer_live_allow_diagnostic_shader=false --native_renderer_skip_unsupported_draws=true
```

Result: the RelWithDebInfo MP native D3D12 build stayed `Responding=True` for
all 5 second samples through 35 seconds, with working set stable around
`835 MB`; the watchdog then stopped the process. This shows the current build
does not immediately enter the Windows not-responding state in this bounded
probe, but it does not prove good frame pacing or final visual correctness.
