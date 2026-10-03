# Rift rock seams

2026-09-26. Two-boundary correction accepted in the headset.
JKO vines remain independently accepted.

## Acceptance

User revisited both diagnostic viewpoints and confirms no seams remain.
Other observed effects and objects remain correct. This closes the reported
Rift seam defect; the third location was not separately reported in this run.
Retain the two-edge negative-control tests and failed-attempt history below.
No further renderer changes or deployment were made on acceptance.

## Ten-image result and missed half-unit boundary

Examined all /tmp/seam/viewpos-{0,1}-{0,1,2,3,4}.png against
/tmp/jka-seams4.log. The first position is (692 1341 630) : 165;
the second is (129 -1468 -1162) : 127. The diagnostic reaches all 827
eligible rock surfaces. Dotted lines remain on solid gray rock, ruling out
the base texture and lightmap as their source. No separate bright vertical
object is evident when the rock is hidden; sky and terrain are revealed behind
it. The lightmap-only images also show the wall's long vertical surface divisions.

The old audit stopped at a 0.1251-unit gap. It missed the OTHER boundary of
surface 9139, which is a 0.5-unit gap:

| Surfaces | Edge XY | Gap | Height |
|---|---|---|---|
| 9135/9139 | (-1740.5,1544) / (-1740,1544) | 0.5 | -5056..4928 |
| 9138/9139 | (-1770,1878) / (-1770.125,1878) | 0.125 | -5056..4928 |

These two boundaries are consistent with the two parallel screen lines. From
position 1 their azimuths are about 121.82 and 119.58 degrees. The first
viewpoint sees the half-unit opening much more obliquely. This is a stronger
geometric diagnosis than the earlier single-edge fixture, but not a substitute
for headset acceptance. In particular, screenshot camera matrices were not
captured, so the correspondence is not a pixel-exact draw-call identification.

Replace the incomplete repair with paired vertical boundary joining up to
0.5 units, still ONLY for static, planar, undeformed two-stage rock3_phong in
t3_rift. Require identical endpoint heights, compatible normals, an axis-aligned
sub-unit discontinuity, and an unambiguous partner. Move matching position
copies to the midpoint (at most 0.25 units), preserving Z, UVs, colors, normals,
indices, batches and collision/save data. Expand batch bounds conservatively.
No extra triangles, per-frame repair, draw calls, winding, depth-bias or global
filtering changes. All debug modes now use the same repaired geometry.

The production loader executed against retail data with ASan/UBSan reports six
pairs and 45 moved vertex copies: the four previously considered full-height
pairs, plus 9039/9159 (0.25) and 9135/9139 (0.5). Attribute/index preservation,
idempotence and excluded-batch behavior pass. Repeat with:

    python3 tools/audit_rift_boundaries.py \
      --game-base "/games/SteamLibrary/steamapps/common/Jedi Academy/GameData/base"
    python3 tools/audit_rock_seam_raster.py --build OpenJK/build-vulkan-clean \
      --game-base "/games/SteamLibrary/steamapps/common/Jedi Academy/GameData/base"

GPU coverage, 390 eye/subpixel cases per boundary and variant, using all three
retail faces and both logged origins:

| Boundary | Original frames with gaps | Old eighth-only repair | Both joined |
|---|---:|---:|---:|
| 0.125 | 18 | 0 | 0 |
| 0.5 | 48 | 48 | 0 |

This explicitly reproduces the omission in the old repair. It is still an
isolated raster test, not a full game replay. All 23 CTest suites pass; both
renderers built and deployed with matching installed/build hashes:

- JKA: d47cd8431807e9570140deca17af5a5339f48c151f7c71c0652d0e48373fd6e2
- JKO: 073b2c2c3766aeb3d16d1a248e58c774cfb7df29d2a3697d0a5c9cff799dba71
- Prior diagnostic pair: build-vulkan-clean/pre-rift-both-boundaries-20260926-gJSDJN/

Retest JKA only: load selost3rift, use normal mode 0, inspect both reported
viewpoints with head movement, plus the third seam elsewhere if convenient.
Check nearby platforms, crystals and lighting. If a line remains, take one
normal and one gray (mode 1) screenshot there, then restore 0. Do not request
another ten-image sweep or a repeat of the accepted JKO vines test.
The acceptance run was requested with log /tmp/jka-seams5.log. Expected records:
the six rd-vulkan-rift-boundary records,
including surfaces=9135/9139 gap=0.500 and surfaces=9138/9139 gap=0.125.

## Previous diagnostic run (historical)

User reports no change after joining. `/tmp/jka-seams3.log` confirms four joins
and 32 moved vertex copies on both map loads. Remove the production repair and
its helpers/tests; do not retain an ineffective alteration of the retail map.
The ray/coverage tests demonstrated real small gaps but did not establish those
gaps as the source of the visible bright lines. Do not infer a fix from that
isolated GPU result again. Historical findings below are not current behavior.

Add `r_vulkanRiftSeamDebug` (default 0, non-archived, no cheats), gated at load to
the static two-stage rock3_phong material in t3_rift. Original geometry, batching
and PVS remain. Mode changes log `rd-vulkan-rift-probe` with affected surface
count and view origin. Shared mode selection applies to both eyes and suppresses
late material/fog/dynamic-light passes on the selected rock during diagnosis.
Other scene objects/effects remain, allowing identification of an unrelated
overlaid primitive. It is not a fix and has no effect outside the scoped material.

Load `selost3rift`, use the latest screenshot's obvious vertical line and keep
roughly the same viewpoint. Log to `/tmp/jka-seams4.log`. Set each mode separately,
close the console, inspect the line while moving the head slightly, and capture
a screenshot for each mode:

| Command | Expected rock appearance | Question |
|---|---|---|
| `r_vulkanRiftSeamDebug 0` | Normal | Baseline |
| `r_vulkanRiftSeamDebug 1` | Solid gray | Is the white line still present? |
| `r_vulkanRiftSeamDebug 2` | Unlit rock texture | Does the line occur in the base texture? |
| `r_vulkanRiftSeamDebug 3` | Lightmap only, no rock detail | Does lighting carry the line? |
| `r_vulkanRiftSeamDebug 4` | Rock hidden, other geometry/effects retained | Does the line remain independently of the wall? |

Restore `r_vulkanRiftSeamDebug 0` and exit. Report whether mode 1 actually changes
the wall containing the seam to gray; this checks material identification, not
just binary deployment. No further JKO test is needed. Build/automated tests do
not count as visual acceptance of a fix.

Diagnostic deployment: both renderers built; all 23 CTest suites and
`git diff --check` passed. The removed CPU joining test accounts for the suite
count reduction; new guards check diagnostic scope, mode clamping/defaults,
absence of production geometry repair and suppression of late rock passes.
Both installed hashes match the build:

- JKA: `6dea398afcd8c75cdac3998817510710ed78867edc3ee50b67cc7fdbbb88cb0f`
- JKO: `b4506016609d615e5143fd7381429d127a1c85f27eb68de4ff2945e507361e57`
- Prior pair: `build-vulkan-clean/pre-rift-material-probe-20260925-OVIN4p/`.

The accepted vine path remains unchanged.

## Rejected endpoint-join attempt (historical)

`/tmp/jka-seams2.log` confirms every bridge was active. The user sees no
improvement and possible worsening at viewpos `(-1204 417 -267) : 102`, shown in
`/home/patola/Downloads/64Gram Desktop/c434d21b2c672769aaa4c6830723aaf1.png`.
Save name corrected to `selost3rift`. Ray checks at the new viewpoint find the
same 9138/9139 gap; this was not an installation or map-selection miss.

Remove the bridge triangles. Match the same tiny vertical boundary gaps, but
require identical endpoint heights (partial spans now rejected). Give both
edges the same midpoint XY, including duplicate copies of their vertices in
the eligible static rock faces. Keep Z, texture/lightmap UVs, colors, normals,
indices and batch identities. No added triangles or passes. Retail audit joins
four edge pairs: 8850/8845, 9030/9024, 9047/9044, 9139/9138. It moves 32 vertex
copies, by at most 0.0625 units each. The partial 8501/8498 pair is excluded.
Map/material restrictions remain. Game collision and saves remain untouched.

`audit_rock_seam_raster.py --build OpenJK/build-vulkan-clean` adds a GPU coverage
sweep over 195 subpixel/eye offsets, with a synthetic negative control. Optional
`--game-base` loads the actual retail triangles. Both use perspective-projected
clip coordinates and the production fragment shader, not the full engine view,
PVS, material stages or lighting. The original gap appears in 21/195 frames;
the joined edge has no uncovered pixels in the region. IMPORTANT: the rejected
bridge also passes this isolated coverage test. Do not claim this reproduces
or explains the entire in-game failure. Joining avoids the extremely thin
additional geometry, but still requires headset validation.

The production CPU repair fixture checks unchanged index/batch/vertex counts,
unchanged non-position attributes, movement bounded by 0.0626 units, identical
joined endpoints, duplicate copies, and untouched excluded maps/materials.
Unit tests additionally reject partial-height joins and off-edge vertices.

Retest: `/tmp/jka-seams3.log`, load `selost3rift`, inspect the new screenshot
location and both earlier seams with head movement. Check nearby stonework,
crystals and lighting. Expect `rd-vulkan-rock-join` reporting four edges and 32
moved copies. No cvar changes. Do not ask for another full JKO vine test.

Endpoint-join deployment: both renderers built, all 24 CTest suites passed,
retail CPU sanitizer audit and GPU coverage sweep passed, `git diff --check`
clean. Atomically installed with matching build/installed SHA256:

- JKA: `9b364846a9c4d914fe36dc1bfaf97ed341f6b440fffb3437188edf662db7c4dc`
- JKO: `3c04a4068f33e44e441954196f07c5964a9e60bfb74f504cd0d0243981fe65f4`
- Previous bridge pair: `build-vulkan-clean/pre-rock-endpoint-join-20260925-h05SOL/`.

The JKO vine implementation is unchanged. Headset confirmation for the rock
join remains pending.

## Rejected bridge attempt (historical)

## Evidence

`/tmp/jka-seams.log` records `maps/t3_rift.bsp (692 1341 630) : 165`.
User's save is named `selost3rift`. Two visible vertical seams were reported here
and at least one elsewhere. Retail BSP ray inspection identifies planar
`textures/rift/rock3_phong` wall faces. Faces 9138 and 9139 have corresponding
vertical edges at (-1770,1878) and (-1770.125,1878), z=-5056..4928. This is a
real sub-grid geometry gap, not a texture UV border or proven tessellation bug.
Earlier quadratic-patch subdivision audits did not cover these planar faces.

## Scoped repair

At world load, only this map/material's static planar triangle boundary edges
are considered. Reject nonvertical/short edges, opposite normals, identical
edges, gaps over 0.1251 units and nonoverlapping height ranges. Bridge accepted
edges with two triangles, interpolating UV/color/lightmap attributes from one
source edge. Do not move original vertices or rewrite original triangles.
Keep one batch per surface; update index spans and bounds before GPU upload,
mark caches and indirect draw groups. No additional draw calls or per-frame
repair work. No changes to game collision or saved games.

The exact production repair, executed offline against the installed BSP with
address/undefined sanitizers, adds five unique strips:

| Surface pair | Gap | Z span |
|---|---|---|
| 8501/8498 | 0.125 | -5056..-2428 |
| 8850/8845 | 0.125 | -368..3392 |
| 9030/9024 | 0.125 | -5056..-816 |
| 9047/9044 | 0.125 | -5056..688 |
| 9139/9138 | 0.125 | -5056..4928 |

8501/8499 describes the same strip as 8501/8498 and is deduplicated to avoid
introducing coplanar competition. This does not establish that every reported
headset artifact is one of these gaps; runtime logs and retest remain necessary.

## Repeatable checks

`python3 tools/check_rock_seams.py` executes the production repair on synthetic
fixtures, verifies map/material/static/deform guards, duplicate rejection,
attribute interpolation, original geometry preservation and updated ranges.
`--game-base '/games/SteamLibrary/steamapps/common/Jedi Academy/GameData/base'`
additionally audits installed retail geometry without editing proprietary assets.
Unit tests cover shared/large gaps, partial spans, reversed edges, incompatible
normals, nonfinite coordinates and nonvertical edges. Source contracts check
that repair precedes upload/cache construction.

## Headset run

Restart JKA, log to `/tmp/jka-seams2.log`, load `selost3rift` using the load menu.
Inspect both previous vertical lines with head rotation and translation. Check
the additional location if accessible, then ordinary nearby walls, crystals,
lighting and stereo depth. Exit normally. Expect five `rd-vulkan-rock-seam`
records and a summary of five closed gaps. Do not change rendering cvars for
this comparison. Test JKO vines separately using `temple-vines-run.md`.

## Verification and deployment

Both renderers built; all 23 CTest suites and `git diff --check` passed. The
optional retail audit above passed under ASan/UBSan. Installed libraries were
atomically replaced and SHA256-verified against build outputs on 2026-09-25.
Previous pair retained in
`build-vulkan-clean/pre-vine-side-rock-seams-20260925-MtFqFt/`.

- JKA: `d5fb3185a305cff1564a05ab1757652a71064af11dc00dfbaf54aceaf836ee1c`
- JKO: `f444de2c89afce0b6aa209f96e7d65c1c80e0e5cfa452d4e39c090bc8054f1b4`

This pair includes the temple-vine side selection fix. Headset results remain
pending; offline tests do not establish visual acceptance.
