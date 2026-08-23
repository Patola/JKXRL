# Vulkan and VR parity ledger

This document records behavioral contracts discovered while replacing the legacy
OpenGL/SDL2 path. A feature is not considered complete merely because it appears
once: its source contract, Vulkan/VR implementation, diagnostic evidence, and
repeatable acceptance test must agree.

## Checkpoints

| Tag | Commit | Scope |
| --- | --- | --- |
| `vulkan-m1` | `59bc5ad` | First playable Vulkan renderer |
| `vulkan-m2` | `a1f01ca` | Attachments and menu scene ordering |
| `vulkan-m3` | `3384672` | Dynamic effects |
| `vulkan-m4` | `b0a8017` | Effects and VR interaction |
| `vulkan-m4-cpp17` | `82b1c19` | Verified C++17 baseline |
| `vulkan-m4-pre-m5` | `4e6287a` | Cinematic parity before shadow work |

The worktree currently contains post-checkpoint changes. New fixes should be
committed by subsystem after focused verification, rather than accumulating into
another large uncommitted checkpoint.

## Renderer shader-stage contract

The authoritative behavior is the parsed Raven shader, executed in authored
stage order by `rd-vanilla`:

1. Resolve each `map`, `clampmap`, `animMap`, `videoMap`, or `$lightmap`.
2. Compute `rgbGen`, `alphaGen`, and the complete ordered `tcMod` chain.
3. Apply the stage's exact blend factors, alpha test, depth test/write, culling,
   polygon offset, and fog interaction.
4. Draw every active ordinary stage in authored order.
5. Draw `surfaceSprites` after the ordinary stages.

Splitting stages into Vulkan passes must not reorder stages whose framebuffer
dependency crosses a pass boundary. Any optimized or combined pipeline must be
mathematically equivalent to the sequence above.

There is also a batching-order difference that must remain under audit. The
legacy path combines compatible surfaces and renders stage 0 across the batch,
then stage 1, and so on. The Vulkan world path currently iterates BSP surface
batches first and completes all selected stages for each surface. These orders
are equivalent only when surfaces do not overlap and no later stage depends on
framebuffer contents produced by another surface. Do not change this globally
until a captured material trace or focused A/B proves it affects the scene.

### Legacy color-space contract

Original JKXR requests an sRGB OpenXR swapchain but explicitly disables
`GL_FRAMEBUFFER_SRGB`, while ordinary textures are uploaded as unsized RGBA.
Consequently, authored texture sampling, multipass blending, and framebuffer
writes operate on the stored byte values rather than a linearized sRGB path.
The Vulkan M1 implementation instead used sRGB textures and an sRGB framebuffer
view. Opaque rendering can hide this because decode and encode approximately
cancel, but translucent multipass materials do not produce the same result.

`r_vulkanLegacyColorPipeline 1` restores the JKXR contract with UNORM ordinary
textures and a mutable UNORM render view over the runtime's sRGB swapchain. If
the OpenXR runtime rejects mutable-format swapchains, initialization falls back
to the prior sRGB-linear path and reports that decision in the log.

### Legacy texture-sampling contract

Explicit material maps are mipmapped by default in the GL renderer and use
`GL_LINEAR_MIPMAP_NEAREST`; UI and cinematic images use non-mipmapped sampling.
The initial Vulkan path uploaded only level 0 and clamped both samplers to it.
Static Vulkan images now receive a complete RGBA mip chain. The repeat/world
sampler may select those levels, while the clamp/UI sampler remains fixed at
level 0. This is especially important for animated translucent materials such
as the starting river, where minification changes both sampled color and alpha.
Focused comparison showed no visible near/far river change, however, so missing
mips were a renderer-contract defect but not the cause of its color/opacity
mismatch.

The world sampler must also use trilinear mip interpolation. Both legacy
renderers default to `GL_LINEAR_MIPMAP_LINEAR`; selecting a nearest mip level in
Vulkan keeps distant terrain transitions artificially sharp through translucent
materials even when the mip chain itself is present.

### Yavin water materials

The starting river in `yavin1.bsp` is definitively
`textures/h_evil/lakewater`. Its shader in `hiddenevil.shader` has four stages:

| Stage | Image | Blend |
| --- | --- | --- |
| 0 | `textures/h_evil/wf3` | `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` |
| 1 | `textures/h_evil/wfn2` | `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` |
| 2 | `textures/h_evil/waterf1` | `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` |
| 3 | `$lightmap` | `DST_COLOR`, `ZERO` |

The temple pool is a different material,
`textures/common/Water_Yavin2`. It combines a constant-alpha base using
`ONE`, `SRC_ALPHA`, a lightmap modulation stage, and an additive stars/detail
stage. River and temple tuning must never share a material-wide alpha override.

Current status:

- Starting-river color now closely matches the flat and Quest references: the
  green component is present but subtle. Submerged boundaries still need a
  focused comparison after the patch-tessellation correction below.
- A river-only reproduction of the GL loader's `r_intensity` and software
  `r_gamma` transfer did not improve the match and coincided with a stronger
  green cast, so it was reverted. Applying one transfer in isolation while the
  rest of the frame remains on the current Vulkan color contract is not valid.
- `r_vulkanYavinRiverOpacityScale` adjusts only the three alpha-blended
  `lakewater` stages. It must multiply each sampled PNG alpha; clamping the
  stage's default `1.0` constant before texture sampling made every value above
  one a no-op. A scalar above one still did not reproduce the reference's
  low-frequency veil, so the default remains the authored `1.0`.
  `r_vulkanYavinRiverStageMask` isolates `wf3` (bit 0),
  `wfn2` (bit 1), `waterf1` (bit 2), and `$lightmap` (bit 3) so the pass that
  preserves terrain detail and introduces the green cast can be identified.
- Stage-mask testing established that the green contribution enters with the
  final BSP-lightmap stage. `r_vulkanYavinRiverLightmapGamma` therefore controls
  only that stage; it does not alter temple water or general world lighting.
- Both `yavin1` and `yavin1b` contain no global BSP fog. A trial camera-distance
  alpha approximation produced no perceptible match and was removed; river
  coverage remains a property of the authored `lakewater` texture stack.
- Reference screenshots show a low-frequency pale blue-gray extinction layer
  beneath the moving detail. It softens submerged geometry boundaries at every
  viewing distance; neither BSP contains local fog brushes that could supply
  it. A base pass before the material stack was almost entirely consumed by
  the three animated layers and then tinted by the final lightmap. Vulkan now
  applies the veil after the complete river material, where it reduces
  submerged contrast without losing the authored motion. The diagnostic mode
  proved all visible river sections use this path. `r_vulkanYavinRiverExtinction`
  controls its coverage and defaults to `0.22`. It never applies to
  `Water_Yavin2`.
- Every `lakewater` surface in `yavin1.bsp` is an `MST_PATCH`. The first Vulkan
  loader incorrectly treated the legacy `r_subdivisions 4` default as exactly
  four segments per quadratic Bezier span. In OpenJK, four is a maximum error
  in world units and subdivision is recursive. Measured river spans require up
  to 16 segments, so the fixed sampler left visibly polygonal water/rock
  intersection contours. Vulkan now derives adaptive power-of-two sampling per
  span from the legacy error test and retains the original 129-sample axis
  limit. This is geometry detail, not texture mip/LOD behavior.
- Native-resolution Quest/Vulkan pairs showed that adaptive patch tessellation
  did not visibly soften the reported submerged facets. The shoreline contour
  is already nearly identical, so tessellation is retained as general BSP
  parity but is not considered the water-compositing fix.
- Legacy draw sorting merges visible world surfaces sharing one shader into a
  single tessellation batch, then renders each material stage across that whole
  batch. Vulkan previously completed all four translucent `lakewater` stages
  for one BSP patch before advancing to its neighbor. Since alpha blending and
  destination-color lightmap modulation are order dependent, this can expose
  patch overlap/triangulation boundaries. River patches are now submitted
  stage-major as one material; color and opacity constants are unchanged.
- Temple pool: base transparency is accepted. Earlier boosts of `3.25` for the
  stars/detail and wake layers now overstate the authored effect under the
  corrected byte-space color path, so both return to `1.0` independently of
  the base transparency.
- `r_vulkanLightmapGamma` isolates the legacy software-gamma operation on BSP
  lightmaps. It defaults to the neutral `1.0`; testing the user's configured
  legacy value (`1.195938`) can establish whether the river's dark green cast
  comes from the previously omitted lightmap transfer without changing diffuse
  textures, menus, or swapchain color handling.
- `r_vulkanMaterialAudit 1` logs parsed stages and first draw selection for both
  materials. The next diagnosis must prove stage availability and execution
  before changing color or alpha.

### World texture filtering

JKXR's GLES renderer defaults `r_ext_texture_filter_anisotropic` to 16. Vulkan
previously used trilinear filtering with anisotropy disabled, causing strong
detail loss on oblique rock and ground textures even at the same headset output
resolution. The Vulkan device now enables `samplerAnisotropy` when supported
and applies up to 16x to the repeating world sampler. Clamp/UI/cinematic
sampling remains non-anisotropic, and BSP lightmaps remain single-level and
clamped as in GLES.

JKA's GLES renderer also defaults `r_picmip` to `1`; the current Linux profile
had archived `r_picmip 0`. Vulkan previously ignored the cvar regardless of its
value. Its world sampler now clamps its minimum LOD to the configured picmip,
which is equivalent to GLES discarding those leading levels without incorrectly
biasing minified surfaces. The renderer default remains `1`, but JKXR's High
Quality UI preset stores `0`; same-resolution Quest comparisons showed the
reference installation using the sharper high-quality result. Use
`+set r_picmip 0` for those comparisons.

`r_vulkanWorldDebug` isolates implicit lightmapped BSP materials at runtime:
`1` draws diffuse texture only and `2` draws BSP lightmap only. Explicit shader
materials remain intact. This distinguishes texture/UV/geometry facets from
lightmap facets without rebuilding or reloading the map; return it to `0` for
normal rendering.

Explicit shader stages now honor the legacy `detail` directive and
`r_detailtextures`. Jedi Academy's GLES renderer defaults this cvar to `0` and
removes marked stages while finalizing a shader; Vulkan previously ignored the
directive and always rendered them. This notably added a 16x-tiled `detail8`
pass to Yavin's `models/map_objects/yavin/ymix` rock material even when the
reference renderer omitted it. Jedi Outcast retains its legacy default of `1`.

BSP vertex colors are preserved from the map data, matching
`R_ColorShiftLightingBytes` under the legacy defaults. Vulkan previously added
a brightness floor to ordinary vertices and replaced very dark colors with a
value derived from each vertex normal. That made dark, vertex-lit Yavin rocks
brighter and exposed their triangle boundaries. Authored vertex alpha is also
retained instead of being forced opaque.

Model stages using `rgbGen lightingDiffuse` are distinct from ordinary vertex
color stages. Vulkan now loads the BSP `LIGHTGRID` and `LIGHTARRAY` lumps,
trilinearly samples ambient light, directed light, and direction at each model's
lighting origin, applies the legacy ambient scale and minimum light, transforms
the direction into model space, and streams diffuse vertex colors for MD3
models. Previously these stages were parsed as unlit texture stages, leaving
Yavin's standalone `rock_b.md3` boulders much brighter than the GLES scene.
`rd-vulkan-lightgrid` and the bounded `rd-vulkan-model-lighting` log records
provide runtime verification.

The same lighting rule also applies when a model surface names an image that
has no explicit shader definition. Legacy `R_FindShader` registers model images
with `LIGHTMAP_NONE` and synthesizes a one-stage `CGEN_LIGHTING_DIFFUSE`
material. Vulkan now does likewise at the model registration boundary. This is
observable on `tree09_b.md3`: its scripted leaf surfaces already requested
diffuse lighting, while the unscripted `tree09.tga` and `tree09c.tga` trunk
surfaces had previously remained fullbright. Packed MD3 normals use the legacy
256-step angular decode rather than treating byte value 255 as a duplicate of
zero.

## VR movement contract

The movement path is:

`OpenXR action -> normalized stick -> usercmd -> ClientThink/Pmove ->`
`friction/acceleration -> PM_StepSlideMove -> collision trace -> player origin`

The stick vector controls desired direction and proportional speed. Full axial
or diagonal deflection must remain sustained without entering a low-speed state.
Diagonal magnitude is normalized by the original movement code; controller code
must not introduce a second nonlinear clamp.

Evidence from the 2026-08-20 log:

- Full input reaches `Pmove` unchanged during the crawl.
- A failing sample retained about 287 units/s horizontal velocity while producing
  zero displacement.
- Neither the ground nor slide `allsolid` branch fired.
- `pm.numtouch == 0` does not exclude a world collision because
  `PM_AddTouchEnt` intentionally omits `ENTITYNUM_WORLD`.

Therefore the controller normalization and `allsolid` recovery are not the
current root cause. A later focused run found no persistent crawl. Its sole
brief anomaly was a physical wedge against two NPC entities: perpendicular
collision planes caused the stock triple-plane stop, and movement resumed when
the actors moved. The movement audit remains available for another regression
round but no further movement behavior change is currently justified.

Acceptance test: sustained full forward, left/right strafe, and both forward
diagonals for at least 30 seconds each, including turning with the right stick.
Partial deflection must remain proportional.

## Tracked-saber damage contract

The tracked controller supplies the physical blade base, direction, previous
pose, and swing velocity. The game then performs swept saber traces, resolves
Ghoul2 and world collision, accumulates victims and damage, applies saber stop
fractions, calls `G_Damage`, and finally invokes NPC pain/death behavior.

The thrown saber uses a separate path and is a regression guard, not evidence
that the direct tracked-blade path works.

### Howlers

Comparison with original JKXR and flatscreen JKA established that direct saber
damage is intentionally suppressed while the player is in
`BOTH_SONICPAIN_START/HOLD/END`: the third-person character covers both ears and
cannot swing. This is not a Ghoul2 failure. In VR, however, tracked hands remain
free and visibly cross the target, while a thrown saber already causes damage.
The legacy animation gate therefore creates a control/render mismatch.

The VR adaptation preserves the original rule for flatscreen and NPC attacks,
but restores normal saber damage when the local player's physical
velocity-triggered swing occurs during sonic pain. The earlier speculative
howler-bounds retry and stop-fraction exception were removed. Diagnostics record:

- nearest howler distance from the physical blade segment;
- howler animation, timers, health, and AI state;
- accumulated victim damage and saber stop fraction;
- health immediately before and after `G_Damage`.

Acceptance test: direct slow and fast horizontal/vertical swings during the
howl, ordinary howler movement, a scripted tree, and an ordinary NPC. Thrown
saber behavior must remain unchanged.

## Controlled recovery protocol

1. Preserve the current dirty tree; do not destructively reset it.
2. Build `vulkan-m4-pre-m5` in a detached worktree as a renderer-only A/B
   baseline for the river.
3. Run one instrumented current build to capture water stage selection,
   step/slide decisions during a crawl, and the howler damage lifecycle.
4. Compare evidence with the legacy renderer and the detached checkpoint.
5. Make one subsystem fix at a time, remove temporary high-volume diagnostics,
   verify its focused matrix, commit, and tag meaningful known-good milestones.

The active Linux launcher loads the engine and renderer from `/usr/lib/jkxr`
and the JKA game module from `/usr/lib/jkxr/base`. A deployment is valid only
after SHA-256 hashes of those installed files match the selected build outputs.
The detached baseline is installed as `rdsp-vulkan-baseline_x86_64.so`; a
command must set `cl_renderer` explicitly during A/B runs so archived config
cannot silently choose the other renderer.

## Deferred work

- Full physical ragdolls.
- Legacy-style glow/blur target or bloom buffer for sabers and authored glow
  effects.
- Evaluate AI-upscaled cinematics while preserving optional compatibility with
  original game assets and licensing constraints.
- Broad x86-64 optimization before the eventual ARM64 port; ARM64-specific work
  remains last because target hardware is not yet available.
