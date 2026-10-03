# Local BSP fog volumes

## Sludge Material Follow-up (2026-09-27; JKO headset accepted)

User accepts the updated sludge appearance, stereo stability and the small
unassigned pool. This closes the reported Artus comparison; it does not imply
separate visual acceptance of JKA local volumes or independent FX attenuation.

Both renderers built; all 23 CTest suites pass. Installed SHA-256 hashes match:

- JKA: `123d4a89735e4e76421d52219a397738f0b2b9d265920218f307b3df105d79a3`
- JKO: `a011f5258d8e189b22a35f89ab11bb957a827ab3a976928cbfeb5c091bd130e3`
- Backup: `OpenJK/build-vulkan-clean/pre-material-noise-20260927-GWwLfv/`.

User comparison in /tmp/sludge2 (jkxr-quest.png, jkxrl-localfog-0.png and
jkxrl-localfog-1.png) confirms the fog-on result is much closer, but Quest's
sludge patterns remain more distinct. The gross uniform wash is gone. Captures
have different animation phases, viewpoints and transient effects; screenshots
alone do not establish an exact fog-density or gamma correction.

Source audit found a separate missing material contract. The stock slime's
first stage requests `rgbGen wave noise 0.5 0.1 0 25`, but the unsupported
wave type became NONE, leaving RGB at unmodulated 1.0. Both stages also request
`tcMod stretch noise 1 0.01 0 0.05`, previously ignored. Restore CPU RGB and
stretch noise using legacy temporal interpolation, phase convention
(time+phase)*frequency, and clamped byte RGB. Existing periodic waves retain
their previous evaluation. No fog formula, density, palette, image upload,
filtering, texture resolution or rendering order changes in this follow-up.

Noise uses a private standardized minstd_rand seed-1001 table, not libc rand:
same sequence on x86-64/ARM64, no game RNG mutation and no per-eye randomness.
This preserves the source interpolation/amplitude contract, not a frame-exact
match to Quest's libc-specific random table. The scoped parser accepts noise
only for RGB/tcMod stage parameters; alpha/deformation noise remains outside
this batch. It applies to materials requesting these operations, not only to
one map. The 2D path gains stretch support; broader 2D RGB-wave parity remains
an independent audit item.

Tests check 1,800 stereo/time samples, authored brightness/stretch ranges,
continuity, negative times, large/invalid inputs, unchanged periodic waves,
parser scope and game RNG isolation. A sanitizer fixture compares 16,385
samples against the original R_NoiseGet4f implementation using matching tables.
Startup logs `rd-vulkan-material-noise` with material, stage and waveform data.

Completed run: keep local fog at 1; inspect outdoor sludge from this viewpoint for
10-15 seconds (including the pulse), then a nearby unassigned pool. Compare
brightness and animated pattern contrast, not a single phase. Preserve
/tmp/jko-sludge-noise.log. Check no stereo disagreement or scenery regression.

## Artus Follow-up (2026-09-26; visual recheck pending)

Deployed both renderers after 23/23 CTest suites and validation-enabled GPU
tests passed. Build/installed SHA-256 values match:

- JKA: `901f087dcb98851aaf5729242a53c09f5dd666e2ff874dd053e5e152401f89f2`
- JKO: `fba14b0137b7df8980846635ac32df4d3ed32d45a851f12d2ca1a3a6d59c52c2`
- Backup: `OpenJK/build-vulkan-clean/pre-fog-liquid-order-20260926-e8qDGi/`.

Runtime confirmation marker: `rd-vulkan-local-fog: liquid=textures/imp_mine/slime
after-terrain-fog=1 boundary-depth-equal=1` (one log line).

First headset run accepted boundary stability, binocular agreement, dry scenery
and characters. Two observations required investigation:

- At logged viewpos (-2121,1280,555), the small pool uses BSP surfaces 17035 and
  17036, both `textures/imp_mine/slime`, both fogNum=-1, at z=320. Its bounds
  (-2368,1280,320)..(-2048,1536,320) are outside both authored local volumes.
  Nearby pools 17058/17059 and 17017/17018 also have no fog assignment. Thus
  no 0/1 difference is expected there. Do not manufacture new fog from the
  material's fogParms alone. Reproduce with audit_local_fog.py --point.
- Screenshots in /tmp/green show the outdoor river brighter and flatter than
  Quest, especially in the distance. The previous geometry fog pass ran after
  the liquid stages, so underlying terrain fog painted over the liquid detail.
  The liquid's own fog also used LEQUAL, unlike legacy seeThrough's FP_EQUAL.

Correction is gated by an authored contract: fogParms + explicit sort
seeThrough + nonempty stages with no opaque/depth-writing/sprite stages.
These static BSP materials now finish in the existing late-water path, after
terrain/model fog and receiver shadows. Their local fog boundary uses an
alpha-blended, non-depth-writing EQUAL pipeline. Global fog math, fog density,
fog RGB, water alpha, all texture data and general culling remain unchanged.
Unassigned pools still have no local fog. This is not a global sorting rewrite
or a new fog-pass policy for arbitrary translucent models.

Added executable production-classifier tests and source ordering guards. A
production-fragment GPU fixture combines a fully fogged bed, a nearer liquid
boundary, additive liquid detail and double-modulated lighting. Correct order
retains two different texels; old ordering deliberately reproduces a uniform
fog-colored result. The fixture checks equal-depth rejection of the nearer
non-depth-writing boundary and runs with Vulkan validation.

Repeat JKO first with the command below, using /tmp/jko-local-fog-followup.log.
Compare the outdoor river at 0/1, especially distant detail and overall
brightness against Quest. Recheck the small pool (no fog-toggle difference
expected), boundary/stereo stability, dry scenery and liquid animation. A
second pair of matched screenshots is useful if brightness still differs;
do not compensate by changing global gamma or color settings.

## Initial Build

2026-09-26: implemented in both renderers; headset acceptance pending.

Both renderer builds succeeded and all 23 CTest suites passed. The GPU raster
test also passed on RX 7900 XTX/RADV with Khronos validation enabled and no
reported errors. Installed files match build SHA-256 hashes:

- JKA: `7f3af9d3ba13623a10a745a6de8f5abbdc3b5b0fca432e3eed23939363d3e1f4`
- JKO: `9c94c758bc7cf55638a300895b1bf9b339caa7ed62f342a25fe5ff7cd6a7d76e`
- Previous installed pair: `OpenJK/build-vulkan-clean/pre-local-fog-20260926-sJnT1u/`.

Only renderer libraries were deployed. No asset archive, save, configuration,
game module or executable was replaced. Existing compiler warnings in unrelated
vegetation formatting and billboard-vector code remain; not changed in this batch.

## Source Contract

- Legacy `tr_bsp.cpp::R_LoadFogs` reads local brush bounds from the first six
  axial sides, and an inward-facing plane from visibleSide. A visibleSide of
  -1 represents fog without an exposed boundary. Global fog has brushNum=-1.
- Vulkan preserves raw BSP fog indexes (-1 is no assignment), including empty
  slots for global/invalid entries. Static world batches retain surface fogNum;
  indirect groups cannot combine different fog assignments.
- Models select a containing/intersecting volume from transformed bounds,
  preferring a fully contained volume, then an overlapping one containing the
  eye. This uses true AABB overlap, not the legacy two-corner partial test.
  The boundary plane is transformed into the same model space used for drawing;
  rotation, translation and scale must not detach the fog from the world.
- `RB_CalcFogTexCoords` and `R_FogFactor` define boundary clipping and a
  square-root density curve. The local Vulkan branch evaluates that curve per
  fragment analytically, rather than interpolating legacy per-vertex lookup
  coordinates and sampling a quantized fog table. Eye classification uses the
  shared scene origin; view depth remains per-eye.
- Local fog replaces the global fog pass for assigned/selected geometry.
  Goggles override it. Existing global/ranged fog math, water stages, alpha,
  lighting, projection, sorting and depth/cull pipelines are unchanged.
- No new render target, readback or resource binding. Existing geometry fog
  draws carry local plane/color/depth through the existing push-constant block.
  Nonintersecting models return before the additional skinning work.

## Limits

This is geometry-fog support, not a general volumetric ray marcher. Bounds and
one authored exposed plane match the legacy brush contract, not arbitrary
convex-medium integration. Model membership uses model bounds, not per-bone
volume selection. Independently submitted FX and particles do not
gain local fog attenuation in this batch. Vegetation was subsequently added
as a separate bounded batch; see [vegetation fog run](vegetation-fog-run.md).
Local cutout masks use base UVs;
animated mask UV transforms are not expanded by this work. Existing global
scoped-fog behavior is preserved; local fog does not acquire a new binocular
range override. These are audit items, not claims of complete fog parity.

## Verified Retail Fixtures

Read-only audit with production volume construction and ASan/UBSan:

| Map | Local volumes | Assigned surfaces | Bounds (game units) |
| --- | ---: | ---: | --- |
| JKO artus_mine | 2 | 980 / 52 | (-4399,-2095,-72)..(4603,239,16); (-2112,3728,272)..(-256,4208,384) |
| JKO artus_detention | 1 | 46 | (-2384,4104,272)..(-96,4584,320) |
| JKA t2_rancor | 1 | 6 | (-3976,3460,1104)..(-3718,4412,1432) |
| JKA vjun1 | 1, plus global | 42 local | (6976,4160,-64)..(7808,5120,472) |
| JKA t3_hevil | 2 | 75 / 74 | (2304,1408,-3072)..(3072,2176,-320); (-2048,1408,-3072)..(-1280,2176,-320) |

Artus slime declares RGB (0.32549,0.635294,0.0156863), depth 256.
JKA Vjun/Rancor fog is black, depth 3456; Hevil black fog is depth 3072.
Yavin1 and JKO yavin_temple have no local volumes. Hoth2 and Rift have global
fog only. The latter four are negative/regression cases, not local-fog demos.

## JKO First

```bash
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jko-local-fog.log
```

1. Load an Artus Mine save near the green sludge. Compare from the same
   viewpoint using `r_vulkanLocalFog 0`, then `r_vulkanLocalFog 1`. Leave it
   at 1 afterward. Look into the sludge at submerged terrain/objects: expect
   green attenuation with depth, not an indiscriminate green room tint.
2. Move above/near the boundary and look across it from different angles.
   Check binocular agreement and stable attachment under head translation.
   Liquid animation and color should remain recognizable. Do not confuse the
   existing green liquid surface with the additional fog below its boundary.
3. A below-surface comparison is useful but optional. In a separate test
   session, `devmap artus_mine`, `god`, then `noclip` permit crossing the sludge
   safely. Do not overwrite campaign saves. Those cheats are not needed for
   the ordinary above-surface check. Do not use arbitrary teleports into BSP
   bounds: parts may be solid or outside reachable space.
4. Check nearby dry walls, characters, cutouts, saber glow, and the red-pulse
   cargo room remain intact. Compare goggles with `la_zoom` if batteries are
   available; toggling back should restore the ordinary fog.
5. Load another level and exit normally. Preserve the log; map load reports
   `rd-vulkan-local-fog` volume bounds and the first actual world/model draw.
   Report whether 0/1 differs, boundary/stereo artifacts, and performance.

## JKA Separately

Use the usual JKA terminal command, logging to `/tmp/jka-local-fog.log`.
Vjun1 checks local black fog alongside accepted global haze/rain; its local
volume is localized, not a whole-map darkening. Hevil's deep water volumes are
another candidate, but compare from the same viewpoint and keep the accepted
water composition intact. Briefly check Yavin river/pool and Hoth global fog.
No need to repeat the ten-image Rift diagnostic or the accepted vine sweep.

## Automated Coverage

- C++ tests: malformed planes/bounds, boundary density, sloped plane,
  full/partial/eye-preferred membership, invalid/global slots, transformed
  model planes including nonuniform scale.
- Headless production-fragment Vulkan raster tests: 45 eye/point/depth cases,
  zero/full fog, cutout holes, and plane-Z isolation from lightmap controls.
- Source guards: raw slot ownership, indirect grouping, level cleanup,
  goggles override, model transforms and shader parameter wiring.
- `tools/audit_local_fog.py --game-base BASE MAP...` reads retail BSPs and
  exercises the production volume builder under sanitizers. It does not run
  the entire runtime loader or establish visual headset parity.
