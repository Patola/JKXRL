# Material geometry, batch 1

Status: focused JKA headset run passed. JKO follow-up accepted on 2026-09-21:
lava/slime have correct colors and deformation; Valley of the Jedi works.
JKO Force Lightning was initially unavailable, then tested and accepted during
the Artus follow-up. Autosprites are the separate next batch described in
`material-autosprite-run.md`. Artus lighting/camera/floor findings are tracked in
`artus-lighting-camera-run.md`.

## Artus follow-up (2026-09-21)

JKO acceptance stopped at black slime with a thin green line and a flickering
decal near crates. `/tmp/jko-deform.log` confirms `artus_mine`, the slime's three
stages, actual slime/lava deformation draws and offset factor/units -8/-8. It
does not identify the decal. No proof yet that either defect was introduced by
the deformation batch rather than being an untested pre-existing omission.

Installed BSP inspection found paired planar liquid faces, with opposing plane
normals and entirely black underside lightmaps. For example, surfaces 16553/4,
16970/1 and 17017/8. The top 16970 lightmap has nonzero illumination; the entire
16971 lightmap rectangle is zero. The slime ends with DST_COLOR/SRC_COLOR
double modulation: submitting that underside after the top erases the result
to black. The current Vulkan BSP path submits both faces when in the PVS;
legacy `R_CullSurface` rejects back-facing planar surfaces first.

Follow-up restores authored plane selection ONLY for static, planar, one-sided
materials with supported deformations. It uses BSP `lightmapVecs[2]` plus an
authored vertex, not inferred triangle winding or global raster culling. A
shared scene origin gives identical selection in both eyes. An 8-unit legacy
margin plus maximum displacement protects near-plane/deformed geometry. The
filtered visibility mask feeds direct/indirect color, light and fog paths.
Two-sided cobwebs, non-deforming surfaces, patches, triangle soups, inline
models and character culling are untouched. No palette/opacity change.

CPU tests cover above/below selection and the near-plane margin. A compositing
test reproduces the black overwrite using production Vulkan blend factors and
checks that excluding the underside preserves the green top. Integration guards
check eligibility and the shared visibility mask. This proves the identified
failure mechanism, not the final headset result.

There is also a separate known omission: local BSP fog volumes are not loaded
by `VK_WorldLoadFogSettings` (only brushNum=-1 global fog). Artus slime declares
green local fog. Do not disguise that omission with a global tint or assert
complete liquid parity after correcting face selection. Noise waveforms in
the slime's RGB/stretch directives are unsupported too. Investigate any
remaining discrepancy separately after this narrowly scoped fix is observed.

The original retest requested the same slime and decal first, with `+set r_vulkanMaterialAudit 1`
and output to `/tmp/jko-slime.log`. `rd-vulkan-deform-faces` records selected
plane filtering; material audit now includes the Artus slime. Check the slime
from above and oblique viewpoints over several pulses. Keep the broader lava,
lightning and Morgan tests paused. Request a screenshot/landmark or save name
for the decal, and whether `r_vulkanDeforms 0/1` changes its flicker. No decal
offset or culling adjustment was made in that follow-up. The next report
accepted lava/slime and supplied a floor screenshot plus camera-room videos.

## JKA acceptance (2026-09-21)

Patola confirmed gently waving cobwebs, undulating water, correct Force Lightning
body effects and no observed regressions. `/tmp/jka-deform.log` confirms actual
draws of both Rift cobweb materials, common/water_1, personalshield,
fullbodyelectric2 and electric; it records deformation A/B commands and normal
shutdown. No Vulkan error or deformation-stream exhaustion was found in the
log. Missing tutorial_video_7/8 messages are the previously recorded startup
asset warnings. This run does not provide nonzero authored move coverage or
JKO acceptance, and is not a quantitative performance capture.

## Contract

`deformVertexes wave`, `bulge` and `move` affect geometry, not just UVs. The
reference is legacy `tr_shade_calc.cpp`, not a new water or lighting preset.
Up to three ordered stages are evaluated in the GPU vertex shader. Wave/move
use scene seconds minus entity shaderTime; bulge uses the scene clock and raw
texture U in radians. Zero-width/zero-speed bulge means constant normal
expansion, including negative expansion. Wave frequency zero ignores spatial
spread. Periodic lookup quantization and the legacy 1023-denominator sine
table are retained; normals are not recomputed, matching the original.

The same GLSL function supplies color, fog, dynamic-light and shadow/depth
positions, including Morgan's shell prepass. One small dynamic-uniform block
is reused for a material/time combination across both eyes and passes. Static
vertex buffers and GPU-skinned streams are not rebuilt for deformation; no
extra draw, render target or readback is introduced. Vertex-shader work and
descriptor changes still have a cost, so performance needs a live check.
The parameter stream is reset with the existing shared-eye frame stream after
the previous stereo submission's queue-idle wait. It has an explicit capacity
check. Non-deforming draws bind identity.
Model/skin/world bounds conservatively include possible displacement; original
stored model centers and collision geometry do not change. Existing PVS and
shadow-distance policies still apply, so arbitrary huge mod-authored moves
across visibility regions are not guaranteed to work.

`r_vulkanDeforms 0` disables this batch immediately; `1` (default) enables it.
This diagnostic is not archived. It does not stop texture scrolling, skeletal
animation, grass wind, weather or ordinary FX sprites. Unsupported deformation
types warn instead of being approximated. No water alpha/palette, depth order,
camera/input, game module, assets or user configuration changed in this batch.

## Automated evidence

- CPU tests: ordered stages, identity, constant positive/negative bulge,
  zero-frequency behavior, scene/entity clocks, negative phases and bounds.
- GPU test executes the production shared GLSL on 10,240 sample vertices,
  including identity after a deformed batch. RX 7900 XTX maximum CPU/GPU error
  was 0.00000286102 game units; validation layer reported no errors.
- Source guards check parser bounds/braces, pass coverage, descriptor resets,
  clock selection and expanded culling/light/shadow bounds. These are structural
  guards, not runtime parser or full graphics-pipeline tests.
- Both renderer builds and all 20 CTest suites passed. The focused JKA headset
  check is accepted above; JKO and nonzero campaign move coverage remain pending.

## JKA first

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_vulkanDeforms 1 \
2>&1 | tee /tmp/jka-deform.log
```

1. Load `t3_rift` and observe a cobweb close enough to see its edges. Alternate
   `r_vulkanDeforms 0` and `1`, waiting 10 seconds each. It should become gently
   wavy, not swing dramatically. The authored maximum offset is only 2.5 units.
2. Load `t2_rancor` near water. Compare the surface edges for 10 seconds per
   setting. Geometric ripples are small (2-unit amplitude); texture scrolling
   continues in both modes. Do not expect a new water color/transparency.
3. Use Force Lightning on an ordinary enemy, checking the electrical body
   shell as well as ordinary model animation, saber glow and shadows. The shell
   expansions are deliberately small, not an enlarged silhouette effect.
4. With `r_vulkanDeforms 1`, briefly check Yavin river/pool, foliage, HUD,
   console, menus and normal combat. Check for eye disagreement, floating
   overlays, disappearing edges or a noticeable performance regression.
5. Exit normally and retain the log. Registration lines identify declared
   stages; `rd-vulkan-deform-draw` confirms actual submitted material/time.
   Each material logs its first submitted use, not every frame.

If finding a cobweb is awkward, this OPTIONAL disposable test uses cheats:

```text
devmap t3_rift
noclip
setviewpos 256 -440 -930 90
```

Wait for level loading before the next command. Two authored cobweb surface
centers are near (256,-272,-948) and (224,-192,-948). The suggested observer
position is derived from BSP coordinates, not headset-tested; stay in noclip
and adjust position if necessary. Do not overwrite campaign saves. Loading a
normal save afterwards restores its gameplay state. No need to use this route
if a suitable saved scene is already available.

## JKO separately

```bash
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_vulkanDeforms 1 \
2>&1 | tee /tmp/jko-deform.log
```

Test `artus_mine` lava/slime, alternating 0/1 for at least 10 seconds each, then
ordinary electrical/shield effects in combat. Morgan's Valley of the Jedi scene
is a useful depth-shell regression check; no new effect is expected there.

## Asset evidence and deferred batch

Installed stock PK3 BSP shader AND surface lumps were checked, not merely
shader-file names. Example surface centers in game coordinates:

| Map | Material | Example center | Expected change |
| --- | --- | --- | --- |
| JKA t3_rift | textures/rift/cobweb | (256,-272,-948) | sin wave, amplitude 2.5 |
| JKA t2_rancor | textures/common/water_1 | (-2464,5536,1444) | sin wave, amplitude 2 |
| JKA vjun2 | textures/common/water_1 | (32,0,8) | wave; may be inline-model local coordinates |
| JKO artus_mine | textures/imp_mine/lava | (720,1568,248) | sin wave, amplitude 1 |
| JKO artus_mine | textures/imp_mine/slime | (722,-695,16) | sin wave, amplitude 2 |
| JKO artus_detention | textures/imp_mine/slime | (-659,4344,320) | sin wave, amplitude 2 |

Stock move declarations found so far have zero move vectors and are not useful
visual motion tests. Move behavior is covered numerically on CPU/GPU, not yet
by an identified nonzero campaign example. Korriban chain surfaces use
autosprite2, which remains disabled pending batch 1 acceptance. Next batch must
test stereo-stable facing, authored center/axis, normals and shadow consistency
without reusing per-eye facing from ordinary effect sprites blindly.

Before any optimization phase, ask Patola for the promised ARM-oriented
measurement constraints. This work does not begin that phase.
