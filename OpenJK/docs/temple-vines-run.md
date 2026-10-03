# Temple-vine cutout coverage

2026-09-25. Accepted: user inspected from every perspective and confirms the
vines are fully transparent and stable. Paired-side fix is retained unchanged.

## Paired-patch follow-up

User confirms transparency with `/tmp/jko-temple-vines2.log`, but the textures
shimmer. The expected cutout registration is present, so selection now works.
The six patches form three exactly coincident front/back pairs: 0/1 at x=-1752,
2/3 at x=-1504, 97/98 at x=-1120. All span y=424..552 and z=960..1024.
Each pair has opposite X normals, reversed control-point order, and mirrored
texture coordinates. Rendering both makes their alpha coverage compete.

Use the shared-view plane visibility mask for one-sided, non-deforming,
depth-masked lightmap patches only when every control point lies on the authored
plane within 0.001 units. Curved/two-sided patches are excluded. These flat
cutouts use a zero near-plane margin to avoid simultaneously admitting both
opposite faces; existing deformable-liquid planes retain their old margin.
No global winding, alpha threshold, filtering or sorting change. Both eyes and
all world passes consume the same surface mask. Log `rd-vulkan-cutout-side` for
each eligible patch and the existing shared-view rejection summary.

Retest with `/tmp/jko-temple-vines3.log`: view the vines from both directions,
move sideways and approach. Check stable cutouts plus ordinary walls and lamp
halos. This run has now passed in the headset.

Both renderers built and deployed with matching hashes; all 23 CTest suites
pass, including paired-side unit checks and production GPU cutout raster tests.
Deployment identifiers and rollback directory are in `rock-seams-run.md`.

## Evidence and scope

JKO flare checks accepted in `yavin_temple`. User separately reports the ceiling
vines near Luke's chamber have opaque/flickering backgrounds. The run log
`/tmp/jko-bsp-flares.log` registers `textures/yavin/temple_vinesalpha` with three
stages. Stock `assets0.pk3`, `shaders/yavin.shader` specifies:

1. Vine image, GE128 alpha test, SRC_ALPHA/ZERO, depthWrite.
2. Lightmap replacement, ONE/ZERO, depthFunc equal.
3. Vine image modulation, DST_COLOR/ZERO, depthFunc equal.

Six BSP patches (0, 1, 2, 3, 97, 98) use this material and lightmap 17. Its
512x256 TGA has 55,944 texels below the alpha threshold and 75,128 above it;
the source image has valid transparent coverage. Legacy tr_shader/tr_backend
honor GLS_DEPTHFUNC_EQUAL. Vulkan retained the parsed value without applying it
to these world finishing passes, so the lightmap could fill the cleared holes.

The first attempt incorrectly assumed the unknown SRC_ALPHA/ZERO blend fell
back to opaque. The actual parser fallback is ALPHA, so the classifier rejected
the material and the opaque lightmap ran before its coverage pass. The installed
library matched the build, and `/tmp/jko-temple-vines.log` registers the vines
without the expected `rd-vulkan-cutout` activation line. The initial GPU test
proved the selected pipeline's behavior, not that real material selection
reached it. Do not describe that attempt as accepted.

`VK_IsDepthMaskedLightmap` now recognizes opaque or alpha first passes in the
three-stage depth-writing
cutout / opaque lightmap replacement / same-image modulation pattern, with
equal-depth non-writing finishers and no surface sprites. Two existing-shader
pipeline variants apply EQUAL without depth writes to the finishing stages,
only for lightmapped BSP batches and without overriding specialized stage
pipelines. Ordinary materials keep their current pipelines and scheduling.
For a matching material only, normalize the first pass to opaque so it writes
coverage before the lightmap replacement. Its intermediate color is completely
overwritten, so this normalization does not affect the final material RGB.
This does not implement that blend pair generally or all depthFunc contracts.

No texture edits, alpha thresholds, geometry, winding, global sorting, water,
shadows, flares, palette, gameplay, save or configuration changes.

## Verification and deployment

- Extend the existing headless lightmap test with the production world fragment
  shader, depth attachment, synthetic alpha cutout and equal-depth finishers.
  Check both texture halves and foreground occlusion. A negative control using
  the old lightmap depth behavior reproduces the filled hole.
- Source-contract guards check selection boundaries and pipeline lifecycle.
- An executable regression compiles the production classifier and verifies the
  parser's ALPHA result selects the path. Rejection cases cover missing depth,
  missing alpha test, non-equal lightmap, different color texture, additive
  first pass and surface sprites. Source guards require scoped normalization.
- GPU test passes on AMD Radeon RX 7900 XTX (RADV NAVI31).
- All 22 CTest suites pass after refreshing generated cvar source references;
  `git diff --check` passes. No cvar values changed.
- Both renderer variants built and atomically deployed on 2026-09-25;
  installed/build hashes match. Deployment backup:
  `build-vulkan-clean/pre-temple-vines-20260924/`.
- JKA SHA256: `4a7e41bb5b273d9778ad07e44be39190fcba885fab904f0b2a9754bc902e768c`.
- JKO SHA256: `d9daececd288e1deff99c872433d00d090bb692673294e7854e07c3f89691782`.

The hashes above identify the rejected first build, not the corrected retest.

Corrected selection build: all 22 suites pass, including the executable
classifier and GPU cutout test. Both renderers atomically deployed with matching
hashes; prior pair retained in `build-vulkan-clean/pre-vine-selection-20260925/`.
JKA: `82924e86e23378654386c66ab21c30dd6b90b190bd5f5f650030a2ca89b4da1c`.
JKO: `4677a173ab3db04373309d24a0ae1f4912c0c5a1c9cc1a65aab558dd9b45e4f7`.
Retest log: `/tmp/jko-temple-vines2.log`.

## Headset run

Restart JKO, log to `/tmp/jko-temple-vines.log`, load `yavin_temple`.

1. Inspect the same hanging vines beside Luke's chamber: background visible
   through the holes, no opaque rectangle or flickering fill.
2. Move sideways, rotate and approach: no eye disagreement or changing coverage.
3. Check nearby walls/floors and lamp halos; ordinary lighting remains intact.
4. Exit normally. Registration should report
   `rd-vulkan-cutout: textures/yavin/temple_vinesalpha coverage-first=1 lightmap-equal=1 modulate-equal=1`.
