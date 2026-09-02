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
| `vulkan-m4-yavin-parity` | `aee0e4d` | Yavin water, texture, and lightgrid parity |
| `vulkan-m4-portal-sky` | `f854ea2` | Authored portal-sky composition |
| `vulkan-m4-portal-decals` | `4961d34` | Portal depth and polygon-offset decal stability |
| `vulkan-m4-stereo-submit` | `5ea40ae` | Batched two-eye command submission |

New fixes should be committed by subsystem after focused verification rather
than accumulated into large uncommitted checkpoints. Annotated tags identify
the headset-accepted recovery points used before broader renderer work.

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

### Rejected broad BSP experiment

Do not enable shader `cull` directives globally on static BSP batches without
first proving the winding contract for every BSP surface type. An August 2026
experiment exposed mixed winding and intentionally two-sided scenery: reversing
the assumed Vulkan face convention restored most walls but still inverted pipe
geometry in `t1_sour`. The same experiment combined dynamic stage `depthFunc`
state with hard-coded offsets for selected signs, so its remaining flicker and
performance behavior could not be attributed safely. It was rolled back as a
unit.

Revisit coplanar flicker in isolated steps: capture the exact surfaces and
authored stage sequence without changing rendering, place one candidate behavior
behind an off-by-default switch, and compare `t1_sour`, `t2_rancor`, Yavin, and a
closed combat area before making it the default. Never use a material-name list
or global face culling as the acceptance criterion.

The small black screens below the `t1_sour` towers retain a subtle flicker in
both original OpenJK and Quest JKXR. Their candidate five-stage desert display
material contains sine, square-wave, inverse-sawtooth, and scrolling additive
layers, so this is a material timing or stage-composition issue rather than
polygon-offset calibration. Headset acceptance with the current material path
reported no conspicuous flicker; keep the screen black and do not restore the
previous white flashes.

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

- Starting-river color, submerged-boundary suppression, and mipmapped surface
  motion now match the Quest reference. Temple water remains independently
  tuned and must be checked whenever river ordering changes.
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

## Dynamic and animated lighting contract

The engine submits dynamic lights between `ClearScene` and `RenderScene`.
Those lists belong to a particular world, portal, or screen-scene submission;
they must not be read from the mutable current scene after submission. The
Vulkan backend snapshots each list with its scene and swaps portal lights with
the portal refdef and entities while rendering the portal view.

Opaque BSP surfaces receive a bounded additive dynamic-light pass after their
base material. Surfaces marked `SURF_NODLIGHT` or `SURF_SKY` are excluded, and
PVS plus surface AABB/radius tests reject unrelated draws. The fragment pass
preserves an opaque stage's alpha mask, attenuates by radius and surface facing,
and does not alter translucent material ordering.

Model materials using `rgbGen lightingDiffuse` combine the BSP lightgrid with
the scene's dynamic lights. Static MD3 surfaces retain per-vertex diffuse
lighting. Applying weighted, bone-transformed normals to every Ghoul2 vertex
raised CPU command-recording time to 40-70 ms in populated scenes while GPU
stereo work remained 2-5 ms, and produced no visible benefit in the reference
scenes. Animated Ghoul2 surfaces therefore retain the verified position-only
skin stream and receive a per-entity hemispherical diffuse tint. This preserves
scene and dynamic-light color at negligible vertex cost; full pose-dependent
Ghoul2 diffuse lighting requires a later GPU skinning/lighting path rather than
returning that work to the render thread.

Animated BSP lighting may author up to four independent lightmap or vertex
color styles per surface. Vulkan retains their handles and style metadata and
applies the current packed `SetLightStyle` RGBA value to the primary slot.
Secondary slots require independent UV/color attributes. An initial
implementation appended those attributes to the shared vertex type, expanding
every animated-model vertex from 56 to 80 bytes and materially regressing
crowded scenes. Secondary composition is therefore deferred until it has a
BSP-only attribute stream; animated models must keep the compact format.
Bounded `rd-vulkan-lighting` messages confirm dynamic world draws and the first
light-style updates. On world load, `rd-vulkan-lightstyles` inventories all
authored style slots even when secondary composition is deferred. Yavin, Hoth,
and `t1_sour` use only style 0. `kor1`, `kor2`, `t1_fatal`, and `t3_bounty`
contain secondary or custom styles and are future acceptance maps for the
dedicated stream.

Acceptance test:

1. A saber, projectile, explosion, or other submitted light produces a smooth
   colored radial contribution on nearby BSP and tints `lightingDiffuse`
   models, without duplicating the material texture or becoming an eye-filling
   quad.
2. Verify primary light-style updates do not alter unrelated surfaces. Once the
   BSP-only secondary stream lands, load `kor1`, `kor2`, or `t1_fatal` and
   verify the authored effects animate identically in both eyes.
3. Portal sky, Yavin river and temple water, `t1_sour` decals, menus, and
   cinematics retain their verified composition.

This checkpoint deliberately does not include stencil shadows, translucent
shadow masks, soft-shadow blur, or the later legacy glow/bloom target.

### Timing protocol

`r_vulkanTiming 1` enables renderer-local timing without changing render
behavior. Every 120 successful stereo frames, `rd-vulkan-timing` reports
average/maximum CPU command-recording time, queue submit/wait time, total GPU
time bracketed across both eye command buffers, active scene-light count, and
opaque stereo model candidate/culled/draw counts.
The accompanying `rd-vulkan-phases` line separates stereo CPU recording into
sky, static BSP, dynamic-light, surface-sprite/weather, model, and dynamic-effect
work, and reports the static BSP stage-draw count. This breakdown includes
authored portal-sky passes and is intended to distinguish open-level BSP costs
from crowded animated-model costs.
`rd-vulkan-model-phases` further divides model work into culling, bone
evaluation, CPU vertex skinning, Vulkan submission, and unclassified setup.
The ranked `rd-vulkan-skin-model` lines report cache hits, newly skinned
surfaces, vertices, and elapsed time for the eight most expensive models.
GPU timestamps are optional: if the selected graphics queue does not expose
them, the report explicitly falls back to CPU-only timing. Disable the cvar for
ordinary play because query collection is diagnostic instrumentation, not the
external end-to-end frame-pacing measurement.

The Vulkan backend rejects ordinary model entities whose conservative local
bounds are entirely outside an eye's left, right, top, or bottom clip planes
before Ghoul2 bone evaluation and surface recording. It deliberately does not
apply near/far rejection, and first-person/depth-hacked models bypass it.
`r_vulkanModelCull 0` disables this optimization for an A/B if a model is
suspected of disappearing at an edge; the default is `1`.

Static root-BSP opaque and global-fog submission uses grouped indexed indirect
draws when the Vulkan device exposes `multiDrawIndirect`. Groups preserve exact
shader, lightmap/style, surface-flag, and vertex-lighting state; per-surface PVS
and Force Sense visibility is represented by each indirect command's instance
count. Translucent materials, inline models, and the Yavin river's stage-major
ordering remain on their established direct paths. The initial `t1_rail`
profile motivating this path measured roughly 9 ms of CPU recording for about
32,000 stereo BSP stage draws while total stereo GPU time remained near 7-8 ms.
After indirect submission, BSP recording fell to roughly 3-4 ms and GPU stereo
time remained about 4-6 ms. Looking backward from the moving train exposed
roughly 180 models and raised model work to about 40 ms, of which 35-39 ms was
CPU vertex skinning. GLM bone indices and normalized weights are therefore
decoded once at model load instead of being unpacked again for every visible
vertex on every frame; pose evaluation, surface selection, and material output
remain unchanged.

### Measured no-shadow baseline

The pre-shadow baseline was captured on 2026-08-31 with a Quest 3 connected
through the Envision WiVRn build. WiVRn rendered at 150% (3096x3243), 50%
foveation, 140 Mbit/s, normal supersampling and sharpening, fixed 90 Hz, and no
spacewarp. The game ran with `r_vulkanTiming 1`; `pidstat` and `amdgpu_top`
sampled the host once per second. The reported effective rate is derived from
the wall-clock interval between each 120-frame renderer report. It is not a
headset compositor or delivered-frame measurement.

| Game and scene | Effective frames/s | CPU record | CPU skin | Stereo GPU |
| --- | ---: | ---: | ---: | ---: |
| JKA crowded `yavin1` ship cinematic | 22.1 | 39.6 ms | 38.0 ms | 2.3 ms |
| JKA `t1_rail`, looking backward | 41.0 | 18.3 ms | 11.0 ms | 5.3 ms |
| JKA `t1_rail`, looking forward | 53.7 | 13.5 ms | 6.6 ms | 4.9 ms |
| JKA `t1_rail`, looking sideways | 62.4 | 11.2 ms | 4.3 ms | 4.4 ms |
| JKA `t1_fatal` combat | 48.4 | 16.4 ms | 14.3 ms | 3.1 ms |
| JKO Mon Mothma cinematic | 68.7 | 13.16 ms | 12.75 ms | 0.79 ms |
| JKO `kejim_post` gameplay | 35.5 | 24.72 ms | 22.60 ms | 2.27 ms |
| JKO `ns_streets` gameplay | 75.1 | 7.35 ms | 5.61 ms | 2.45 ms |

JKA's crowded ship process consumed about 93% of one CPU core while total GFX
activity averaged about 21%. JKO showed the same imbalance: the Mon Mothma
cinematic used 93% of one core with 21% total GFX activity, and `kejim_post`
used 90% of one core with 24% total GFX activity. WiVRn media-engine activity
held near 74% throughout both games. JKO `ns_streets` rose to 33% total GFX
activity but needed only 57% of one CPU core on average. The hottest sampled
GPU-junction temperature was 70 C.

The dominant limit is CPU Ghoul2 vertex skinning, not Vulkan execution or GPU
fill. The stable Mon Mothma view is especially diagnostic: Kyle and Jan alone
consume about 12.75 ms of CPU skinning while their complete stereo GPU work is
under 0.8 ms. Apparent GPU headroom therefore does not justify an expensive
per-caster CPU submission path.

Shadow implementation is subject to these gates:

- `r_vulkanShadows 0` must preserve the verified no-shadow command path and
  stay within 3% of the corresponding baseline effective rate.
- Camera-independent shadow data is generated once per stereo frame and reused
  by both eyes. No shadow-map pass may be duplicated per eye.
- Animated casters reuse the frame's decoded pose and skinned vertices. Shadow
  collection must not invoke a second Ghoul2 skinning pass.
- At the initial quality level, shadow CPU recording may add at most 0.35 ms on
  average and 0.75 ms to a 120-frame report maximum. Stereo GPU time may add at
  most 1.5 ms on average and 2.0 ms to a report maximum in `t1_rail`.
- Shadow timing is reported as dedicated CPU collection/recording and GPU-map,
  mask, and blur phases so regressions cannot hide inside general model time.
- Resolution, caster/light limits, blur quality, and a complete off switch are
  runtime cvars. Allocation and pipeline creation happen outside ordinary
  frame recording.

The first shadow acceptance loop must repeat the crowded JKA ship, all three
`t1_rail` view directions, `t1_fatal`, the JKO Mon Mothma cinematic,
`kejim_post`, and `ns_streets` with shadows off and on. Visual acceptance alone
is insufficient if any budget above is exceeded.

## Scoped aiming contract

Weapon traces in scope mode follow the stabilized headset/weapon forward axis.
The scope reticle must therefore be projected at that same optical direction in
each asymmetric OpenXR eye projection. It must not inherit `cg_hudStereo`,
which intentionally places the ordinary HUD at a finite binocular depth and
causes the reticle to disagree with the shot ray. Vulkan detects both the E-11
scope artwork and the Tenloss overlay and applies the exact projection-center
offset derived from each eye's tangent FOV.

Acceptance test: aim the E-11 and Tenloss center marks at a small surface point
at medium and long range, fire several shots, change Tenloss zoom, and exit each
scope. The impact axis, scope fusion, circular Tenloss mask, and post-scope
weapon state must all remain correct.

## VR interaction contract

The usable hint and activation must test the same source. With a gesture held,
that source is the corresponding tracked hand; without a gesture, the primary
thumbstick use follows the headset/player view ray. Extended hands also perform
a latched bounds-contact check so moving onto a fixture after crossing the
gesture boundary can activate it once. The latch clears when the hand leaves
the target or the gesture ends, preventing a multi-use script from firing every
frame. Successful activation retains the controller haptic response.

An advertised use target owns the reaching gesture before Force Push/Pull is
resolved. This prevents one extension from both using a fixture and emitting a
Force power. One-shot `misc_model_breakable` fixtures stop advertising success
after their use script clears `BSET_USE`; touching or directly using the spent
fixture produces the stock panel-failure sound instead of silently doing
nothing. The physical target continues to own the gesture in that unavailable
state, so rejection feedback cannot misfire as Force Push.

The bomb fixtures and usable E-Web in `t1_fatal` are the current acceptance
pair. Model-specific `rd-vulkan-eweb-audit` lines list every E-Web surface,
parent, effective hide flags, shader, and draw state. This distinguishes the
authored destroyed state from a Vulkan multipart-model failure without changing
the Ghoul2 `NODESCENDANTS` behavior shared by character dismemberment.

The fixture interaction contract was hardware-verified on 2026-08-25: reaching
into an available fixture activates it once without emitting Force Push, and a
second reach after deactivation produces the rejection sound.

The E-Web is a static seven-bone Ghoul2 model, not a ragdoll. On destruction,
the game applies `NODESCENDANTS` to `eweb_damage`; the complete root surface
must remain because its `cannon_Yrot` geometry includes the spindle joining the
turntable to the tripod. Filtering that bone leaves the root base visibly
disconnected after destruction.
`r_vulkanEwebCull` isolates its face-orientation discrepancy: `0` draws
two-sided, `1` culls back faces, and the default `2` culls front faces. Only
this model's opaque stages use the selected pipeline. Two-sided destroyed
rendering did not remove the displaced remnant, ruling out face culling as its
cause. Destruction now explicitly freezes `model_root` at frame zero because
resetting `s.frame` alone does not clear a Ghoul2 firing/recoil override.
The cgame rider path also refuses to restart recoil on an E-Web whose health
has already reached zero.
`rd-vulkan-eweb-state` logs every surface's effective flags and complete bone
animation state so this reset can be verified independently of appearance.

Acceptance test: activate one `t1_fatal` bomb once, then withdraw and extend the
off hand over it again. The first action runs the script with haptic feedback;
the spent fixture no longer shows the use hint, the second attempt emits the
failure cue, and neither attempt emits Force Push. Force Push must still work
normally after moving the hand away from the fixture.

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

The August 27 sustained reproduction superseded the earlier samples. Values
read directly after `xrGetActionStateVector2f` fell from a near-full diagonal to
roughly 0.1-0.3 magnitude while the physical stick remained held. Filtering,
`usercmd_t`, simulation, prediction, and the final rendered view all followed
that attenuated value correctly. The crawl therefore originates at the OpenXR
action-state boundary rather than collision or camera composition.

The locomotion conditioner arms only after magnitude reaches 0.82. If that same
direction then collapses below 0.58 without first crossing center, it preserves
the outer magnitude until the raw signal recovers or the stick is centered for
75 ms. Partial movement beginning from center remains fully proportional, menu
and turning sticks bypass the conditioner, and
`vr_openxr_stick_dropout_guard 0` provides an immediate A/B. Debug transitions
use the `jkxr-stick-dropout` prefix.

Acceptance test: sustained full forward, left/right strafe, and both forward
diagonals for at least 30 seconds each, including turning with the right stick.
Partial deflection must remain proportional.

Smooth turning preserves the original 72 Hz angular response but integrates it
using elapsed milliseconds. The turn rate must therefore remain responsive and
consistent when renderer performance or headset refresh changes; rebuilding the
engine must not restore the legacy per-input-frame increment.

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

## E-Web wreck diagnostic

The intact E-Web and its destroyed wreck share
`models/map_objects/hoth/eweb_model.glm`. Destruction turns off the
`eweb_damage` surface with `G2SURFACEFLAG_NODESCENDANTS`; the surviving
`eweb_cannon` surface contains the tripod and several independently connected
mesh components.

The following hypotheses have been tested without changing the floating wreck
part and are therefore rejected:

- front-, back-, and two-sided culling selection;
- failure to store the `eweb_damage` surface override in Ghoul2 state;
- accidental use of a different GLM LOD: the E-Web asset contains exactly one.
- deleting root-surface components according to their dominant base/swivel
  bone; that experiment removed valid support geometry and was reverted.
- freezing `model_root` at frame zero and suppressing dead-gun rider updates;
  the hardware result was unchanged, so both changes were reverted.

The captured transition confirms that `eweb_damage` and its `eweb_alpha` child
are hidden after destruction while `eweb_cannon` survives. The remaining mesh
is split between the base, swivel, and three tripod bones. Vulkan now skins
normals with the same weighted bone transforms as the legacy Ghoul2 path; it
previously transformed positions but left normals in bind-pose space.

Temporary connected-component and surface-color instrumentation distinguished
authored wreck geometry from an incorrect bone transform or a second model
submission. It was removed after the renderer fault was identified, so normal
builds carry no E-Web-specific vertex coloring, component graph, or transition
logging.

The August 27 synchronized capture disproved the debris hypothesis. Generic MD3
chunks move, fade, and stop being submitted at their authored four-second
lifetime, while the reported object remains. The component experiment also
showed that the surviving root surface is the authored tripod wreck rather than
a duplicate upper cannon: several connected leg/support pieces span multiple
bone regions and cannot be removed by dominant-bone labels.

The August 29 hardware run disproved the root-recoil hypothesis. It logged
`model_root` frozen at `frames=0..1`, `speed=0`, and the reported assembly was
visually unchanged. Inspection of the seven-bone GLA also showed that frames
zero through two are identical and the later recoil frame only changes
`cannon_Xrot`, while the suspicious root components are primarily weighted to
`base` and `cannon_Yrot`.

The surface-color capture identified the renderer fault. Intact
`eweb_cannon`, `eweb_damage`, and `eweb_alpha` submissions appeared green,
magenta, and cyan respectively, but the post-destruction floating assembly was
normally textured. It therefore was not one of those submitted surfaces. On a
pass where destruction hid every translucent Ghoul2 surface, Vulkan treated
zero draws as an unhandled entity and fell through to the static `hModel` path.
That path redrew default bind-pose geometry. A valid supported Ghoul2 hierarchy
is now authoritative even when all of its surfaces are intentionally hidden in
the current pass; only entities without a handled Ghoul2 model may use the
static fallback.

Acceptance test: inspect and destroy the E-Web in `t1_fatal`, then wait at least
ten seconds. The intact model must remain unchanged; after destruction the
tripod wreck and ordinary short-lived debris remain, but the detached upper
cannon assembly must not persist.

Most `hoth2` E-Webs are map-authored with the invulnerable spawn flag and are
not destruction tests. Only `eweb2` at `(3154, -1220, 1042)` is vulnerable; it
has 500 health rather than the default 250.

## Movement pipeline diagnostic

Movement evidence must distinguish three different cases:

- raw OpenXR stick loss before filtering;
- filtered movement failing to reach a newly built `usercmd_t`;
- a correct command being clipped by game collision or movement state.

`jkxr-stick-pipeline` reports the first mismatch, `jkxr-movement-pipeline`
reports the second, and `jkxr-movement-debug` plus `jkxr-movement-trace` cover
the third. The controller summary labels its command as `prevCmd` because input
processing runs before the next command is built; comparing that old command
to the current stick produced false one-frame mismatch diagnoses.

The two low-speed traces at `(14831.87, -40.03, 440)` in the latest log belong
to `t1_rail`, not `t1_fatal`: full commands reached movement but hit two
perpendicular world planes. They are a collision-corner stop and not evidence
of the original analog crawl lock. The reported `t1_fatal` events require the
new synchronized pipeline diagnostics before their cause can be classified.

The August 29 `t1_fatal` capture identified a separate sustained input failure.
The left stick changed from `(0.077, 0.997)` to `(0.051, 0.402)`, then OpenXR
reported exact zero while the user continued holding it. The old compensator
discarded its latch after 75 ms at center, making the later zero command
indistinguishable from a release. The conditioner now only enters compensation
after an abrupt outer-to-attenuated transition and preserves the latched vector
through zero samples while the controller's thumbstick-touch action remains
active. A touch release, direction change, selector activation, or disabled
movement clears the state. Debug summaries include both controllers' touch
bitfields.

The following hardware capture reported another crawl, but it did not reproduce
that input failure. Full conditioned movement reached full-strength
`usercmd_t`s and acceleration. Every low-speed interval was stopped by world
collision, including pairs of perpendicular planes at exact brush boundaries
such as `(-1296.125, 496.125)`. This is collision-corner trapping, not loss in
the OpenXR, response-curve, or command stages. Future locomotion cleanup should
separate OpenXR acquisition, dropout conditioning, response mapping, and
`usercmd_t` projection into replayable stages. It must retain the original
`pmove` behavior for stairs, slopes, water, wall moves, knockback, and vehicles;
replacing that simulation cannot repair a correctly diagnosed input dropout.

## Force gesture sampling contract

The original Force Push/Pull gesture required one controller update above
`vr_force_velocity_trigger` followed by another update below it. A fast gesture
could begin and end between rendered input samples, so slowing the arm made it
more reliable as frame rate fell.

The primary velocity latch remains, but it is now Force-specific rather than
sharing the saber/melee attack latch. A separate 32-sample timestamped radial
history covers up to 320 ms and is evaluated while the hand is moving, instead
of waiting for velocity to fall and looking back only five rendered samples.
A successful dispatch has a 250 ms cooldown, during which stale motion is
discarded, and an active world-use target owns the off-hand gesture. With
`vr_controller_debug 1`, accepted and rejected candidates log their source,
radial delta, sampled speed, palm direction, history size, and displacement age.

Acceptance test: perform deliberately slow and deliberately fast Push and Pull
gestures against enemies at both good and poor frame rates. Each physical gesture
must trigger at most once; interacting with a fixture must not emit a Force
gesture. Insufficient Force energy feedback remains deferred.

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

## GLM LOD parity objective

The first implementation checkpoint is complete and awaits visual acceptance.
The Vulkan GLM loader now walks every `mdxmLOD_t` using each block's `ofsEnd`.
LOD 0 remains the authoritative hierarchy and bolt metadata, while every level
retains its own vertices, triangles, bone references, and GPU buffers. A strict
loader rejects the complete model if any level has an invalid table, surface
index, vertex, triangle, or bone reference instead of silently constructing a
partial model.

The runtime selection reproduces the legacy `G2_ComputeLOD` inputs:

- projected radius uses the center VR view, model scale, entity radius,
  `r_lodscale`, `r_lodbias`, `CGhoul2Info::mLodBias`, and `RF_G2MINLOD`;
- the result is cached once per model instance per frame and reused by both VR
  eyes and all material passes, preventing eye-dependent selection or popping;
- `r_vulkanGLMLod -1` selects automatically, while values from `0` upward force
  that level and clamp to the number available for each model;
- `r_vulkanGLMLodAudit 1` logs each model's LOD inventory once, and value `2`
  also logs selection changes with distance, projected radius, bias, level
  count, and forced state; and
- `r_vulkanTiming 1` adds an `rd-vulkan-glm-lod` sample containing per-level
  stereo selections and selected vertex and triangle counts.

An offline structured audit of the installed PK3 assets found 129 JKA GLMs, of
which 87 have multiple LODs, and 71 JKO GLMs, of which 54 have multiple LODs.
All offset chains and surface indices validated. Representative triangle counts
are 4,956/2,668/1,172/645 for JKA Jan, 2,921/1,908/776/476 for a stormtrooper,
and 2,948/1,953/887/509 for Kyle.

The forced-level runtime log proves that distinct geometry is selected even in
scenes where the authored lower LODs preserve silhouettes too closely to be
obvious in a headset. For example, Rosh changes from 3,150 to 866 triangles and
Kyle changes from 2,948 to 509 triangles. This mechanically accepts the loader
and selector. Automatic mode must still be compared with the legacy or Quest
renderer along a fixed near-to-far path, with no stereo mismatch, surface loss,
skin error, animation error, bolt error, or unstable threshold.

## GLM compute skinning objective

Ordinary Ghoul2 deformation is dispatched once per visible surface and pose
before the first eye render pass. Its output vertex range is cached and reused
by both eyes and every material pass. CPU skeleton evaluation, bolts,
attachments, collision queries, and gameplay remain unchanged. Disintegration
uses the CPU deformation path because it mutates vertex position and color in a
way that is intentionally separate from ordinary skinning.

`r_vulkanComputeSkinning 1` enables the path and `0` provides a direct CPU A/B
fallback. A graphics queue without compute support, optional resource creation
failure, a malformed bone range, stream exhaustion, or a surface without its
compute descriptor falls back surface-by-surface to CPU skinning rather than
preventing renderer startup. `r_vulkanTiming 1` reports compute dispatches,
vertices, command-recording time, and CPU fallbacks for each 120-frame sample.

Acceptance requires correct customization models, crowded ship actors,
attachments, ordinary gameplay animation, and Tenloss disintegration. The
crowded ship capture must show a material reduction in CPU skinning time and no
increase in GPU stereo time large enough to erase the frame-time gain.

The first JKA headset acceptance passed without animation, attachment, surface,
stereo, or disintegration regressions. In stable crowded-ship samples, CPU
command recording fell from roughly 41 ms to 7.6 ms and measured skinning work
from roughly 39.5 ms to 5.2 ms, while GPU stereo remained near 2.5 ms. Ordinary
frames reported no CPU fallback; charged Tenloss disintegration exercised the
intentional CPU path and remained visually correct.

The JKO headset acceptance also passed. `ns_streets` and its cutscenes were
visibly smoother, with no reported character, animation, attachment, stereo,
scope, or cinematic regression.

## Animated material and VR FOV contracts

Vulkan now retains every image and frequency from `animMap`, `clampanimMap`,
and `oneshotanimMap` stages. Ordinary stages advance from scene time; model
entities carrying `RF_SETANIMINDEX` select the authored frame with `skinNum`,
matching the legacy renderer. `rgbGen wave` is evaluated independently, so a
charged shield/ammo station can pulse its additive glow while frame 1 selects
the authored black/depleted image.

World scenes with `refdef.override_fov` now rescale the horizontal and vertical
OpenXR tangents independently by the game-to-headset FOV ratio. The asymmetric
optical centers and stereo view poses remain intact. The already accepted
Tenloss scope retains its separate circular zoom path; Force Speed and other
legacy override effects use this new path.

Acceptance requires a charged JKO shield station to be visibly illuminated and
animated, then visibly depleted after its reserve reaches zero. An ammo station
uses the same contract. Force Speed in both games must produce a centered,
binocularly comfortable transition into its widened FOV, hold that projection
for the active duration, and return cleanly to the normal projection without
disturbing HUD, controllers, scopes, or ordinary head tracking. The inherited
game-side envelope had accidentally commented the authored FOV amount out of
its hold branch while retaining it at both transition boundaries; JKA and JKO
now keep the full amount during the hold.

## Force Speed motion-blur contract

Both games expose `Force Speed Motion Blur` in the startup and in-game
Advanced Video menus. It controls the archived `cg_forceSpeedMotionBlur` cvar
and defaults to enabled. Because those menus and their localized strings live
in game-specific PK3s, a valid deployment must rebuild `z_vr_assets_jka.pk3`
and `z_vr_assets_jko.pk3` and refresh the higher-priority OpenJK/OpenJO home
copies as well as the packaged copies.

The cgame supplies a normalized effect envelope in `refdef_t`, using the same
300 ms entry, held interval, and 200 ms exit timing as the accepted Force Speed
FOV change. Scope views and live cinematics explicitly suppress it. While the
effect is active, Vulkan renders each eye's world into a private color target
and blends the preceding image from that same eye over the current scene. This
produces temporal movement trails instead of a current-frame radial zoom. The
history is refreshed after each eye is composed and invalidated whenever Force
Speed stops, while screen-space HUD and controller elements are drawn afterward
at full sharpness.

Rapid physical headset rotation or translation attenuates the history
contribution. In-game locomotion therefore retains the speed trail without
smearing ordinary head movement or mixing eye histories.
`r_vulkanForceSpeedBlurStrength` defaults to `1.0` and is a renderer-side
tuning control; the user-facing menu remains a simple on/off choice.

Acceptance requires a smooth temporal blur ramp into and out of Force Speed in
both eyes, binocularly stable world trails, sharp HUD and controller overlays,
unchanged scope behavior, no physical-head-motion smear, and no effect when the
menu option is off. Ordinary rendering must continue on the direct swapchain
path when the effect is inactive.

The first JKA headset acceptance passed on 2026-08-31: the temporal trail gave
a clear impression of accelerated movement, remained comfortable in both
eyes, and left ordinary rendering unchanged after Force Speed ended. A brief
frame-rate dip seen once in `yavin2` was not reproducible in `t2_rancor` and
had no corresponding GPU-time spike, so it remains unconfirmed rather than a
motion-blur regression.

## OpenXR continuity contract

### Cinematic texture ownership

The legacy cinematic system reuses integer client IDs after a stream stops.
Vulkan material registrations can outlive those decoder clients, so a client
lookup must select the newest active registration rather than the oldest
historical texture with the same ID. Otherwise decoded `videoMap` frames are
uploaded into a stale menu texture while the current in-world display remains
black, as happened to JKO's Mon Mothma hologram.

Raw cinematics also reuse a client ID across different dimensions. There must
be exactly one raw texture owner per client. A dimension change replaces that
texture's image in place while retaining its texture handle and descriptor
sets; it must not append another same-client entry every frame. Repeated
`first raw cinematic frame` messages or growth toward the 4,096-texture limit
during one movie is an acceptance failure.

### Procedural effect primitives

JKO's Valley of the Jedi pool illumination uses the Raven `RT_CYLINDER`
contract: `origin` and `oldorigin` are its endpoints, `radius` and `backlerp`
are the two ring radii, and `axis[0]` is its longitudinal direction. Vulkan
must generate the same wrapped 8-to-40-segment cylinder, including the legacy
tapered-cone case. Aggregate effect diagnostics report cylinder counts so a
submitted but unsupported primitive cannot silently disappear again.

### Ghoul2 entity transforms

A submitted `refEntity_t::axis` marked with `nonNormalizedAxes` is the
authoritative scaled model transform. The game-side `ScaleModelAxis` helper has
already folded `modelScale` into normal scaled Ghoul2 submissions, so the Vulkan
model matrix must not multiply those axes by `modelScale` again. Doing so scales
actors twice while `G2API_GetBoltMatrix` scales its result once, separating
attached geometry from bolt-derived effects.

Vulkan applies `modelScale` to unscaled axes, such as the tracked first-person
saber-hilt submission, and when it has to synthesize missing Ghoul2 axes from
`refEntity_t::angles`. This keeps scaled actor geometry, attached weapon models,
and effects such as saber blades in one coordinate space without changing VR
hilt size.

`re.Shutdown(qfalse, qfalse)` is a soft renderer flush used while connecting,
loading a map, and parsing a new game state. Its explicit legacy contract is to
retain the window and graphics context. The Vulkan renderer must therefore keep
the OpenXR instance, session, reference spaces, actions, Vulkan device,
swapchains, persistent image cache, and most recently released swapchain images
alive. Model, skin, and animation registrations are a per-map epoch and must be
reset: game code requires the normal and cinematic Ghoul2 GLAs to receive
consecutive handles. Transient scene collections and borrowed CGame/UI pointers
are also cleared. A full teardown remains reserved for `destroyWindow == true`,
such as `vid_restart` or process shutdown.

Every renderable OpenXR frame must submit a composition layer. A successful
screen-layer or stereo-projection render remains valid until a successful
render in the other mode replaces it. During synchronous map and cinematic
handoffs, the renderer re-submits that last released image with its captured
pose and FOV. A genuinely empty engine frame renders opaque black into both
eyes. It must never expose the WiVRn compositor background, and diagnostic eye
colors or markers must never substitute for the black fallback.

The log records each soft flush as `soft renderer shutdown reset model epoch`
and reports any retained-image handoff. `ending renderable frame without a
layer` is an acceptance failure. A single game process should initialize
Vulkan/OpenXR only once across ordinary map and savegame loads.

Acceptance requires both games to remain compositor-opaque through startup,
menu-to-load, load-to-cinematic, cinematic-to-gameplay, gameplay-to-load, and
cinematic-skip transitions. The preferred result is the last frame or authored
loading/intermission image; black is acceptable where the engine has no image.
The WiVRn background must never become visible.

## VR console presentation

The legacy console derives its columns from the native render-target width.
That produces hundreds of tiny glyphs at supersampled headset resolutions and
places the edit line outside the comfortable central field of view. Under the
Vulkan renderers, the same console buffer, command history, completion, and
scroll controls instead use a fixed 120-column layout with 40 visible output
rows. They are drawn in a centered translucent panel with the edit line pinned
near its lower center.

While an active world is present, opening the console must not promote the
composed eye image to a mono OpenXR quad. Vulkan retains the ordinary stereo
projection and tags only the console's rectangles. Those rectangles are scaled
about the optical center to 56% width and 35% height, with reduced binocular
disparity placing the panel at a calibrated apparent distance of about four
metres. Its calibrated downward center offset is retained as its dimensions are
tuned. The world therefore remains fully stereoscopic behind the head-locked console.
Menus and cinematic screen layers retain their captured world-locked behavior.

Acceptance requires readable, fused text and cursor rendering; an input line
that remains in view for long commands; working history, completion, and
scrollback; stable head-locked placement; and an artifact-free return to the
world when the console closes.

Headset acceptance confirmed the 56% by 35% panel is comfortable and usable,
preserves stereo world rendering behind it, and should remain at this size.

The active renderer owns the OpenXR session and therefore must also own its
vibration output action. Vulkan adds that action to the same `jkxr_gameplay`
action set used for tracked input, binds it to both controller haptic paths, and
exposes a small renderer API accepting a hand, duration in milliseconds, and
normalized amplitude. The executable preserves the inherited controller
channel mask while routing every existing haptic event to that active action.
The legacy GL-side action remains only as a fallback for its own session.

Force Push/Pull emits one 120 ms, full-intensity off-hand pulse from
`ForceThrowEx`, after health, state, debounce, cinematic, ability, and Force
energy checks have accepted the action. A rejected gesture therefore produces
no false success pulse. `vr_haptic_test [left|right|both] [duration_ms]
[amplitude]` bypasses gameplay and validates the renderer/runtime path directly.
With `vr_controller_debug 1`, every accepted Vulkan submission records its
hand, duration, and amplitude, while OpenXR failures report their result code.
Quest 3/WiVRn headset acceptance confirmed independent left/right diagnostic
pulses, successful Force Push/Pull feedback, and right-hand saber feedback.

The fallback haptic queue stores durations in milliseconds, while `ToXrTime`
accepts seconds. It converts milliseconds to seconds before constructing the
OpenXR nanosecond duration for every controller profile. The inherited code
performed this conversion only for Vive controllers, leaving Touch/WiVRn
requests three orders of magnitude too long.

The packaged launchers content-compare both the game module and VR asset PK3s
against the engine's per-user `base` directory before every launch. The game
module must be synchronized alongside its executable whenever the shared VR
state changes; otherwise OpenJK can silently load an older module left in the
game directory and produce an executable/module ABI mismatch. Timestamp-only
copying is insufficient for local builds and package replacements.

## Deferred work

- Full physical ragdolls.
- Complete the automatic GLM LOD visual and performance regression matrix.
- Tune tracked Force Push/Pull thresholds from captured gesture diagnostics as
  needed. A recognized gesture that cannot run because energy is insufficient
  should produce an
  audible rejection cue and controller haptic instead of failing silently.
- Legacy-style glow/blur target or bloom buffer for sabers and authored glow
  effects.
- Deliberately improve beyond original/Quest behavior by extending and tuning
  `surfaceSprites` vegetation draw/fade distance after profiling its CPU,
  geometry, and fill-rate cost. Preserve world anchoring, stereo stability,
  wind animation, and authored density while replacing Yavin's conspicuous
  few-metre pop-in with a substantially longer, gradual transition.
- Evaluate AI-upscaled cinematics while preserving optional compatibility with
  original game assets and licensing constraints.
- Broad x86-64 optimization before the eventual ARM64 port; ARM64-specific work
  remains last because target hardware is not yet available.
