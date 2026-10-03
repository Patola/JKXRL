# Local fog on generated vegetation

Implemented 2026-10-02. Headset run completed 2026-10-03: Patola confirmed
reduced vine contrast with the toggle on at both local-fog viewpoints and no
change at the dry control. The supplied viewpoints worked. No other issues
were reported. The initial closely spaced viewpoints did not establish a
visible distance progression. The wider-separated follow-up was subsequently
confirmed: clear attenuation with distance. This batch is headset accepted.
Scope is authored
local fog on eligible generated plant batches, not new atmospheric haze or
independent FX fog.

The run log `/tmp/jko-vegetation-fog.log` confirms 1,647 eligible batches and
an actual draw of surface 2720, fog 10, depth 512. The two fogged positions
are only about 94 game units apart. For the nominal target and forward view,
the analytic ramp changes approximately from 0.59 to 0.73; actual plant
positions, head pose and existing scene fog affect the visual comparison.
The log does not measure rendered per-fragment attenuation. There is no
runtime change in response to this report.

Both settings means `r_vulkanLocalFog` (master local-fog switch, enabled in the
launch command) and `r_vulkanVegetationFog` (the plant-only comparison toggle).
Each is global across viewpoints, not a separate setting for each location.
The last recorded plant toggle in this run was 0; it is non-archived and the
provided launch command explicitly restores 1 next run.

## Contract

Legacy `RB_DrawSurfaceSprites` passes the parent's fog index to SQuickSprite
only when the shader has a fog pass. Preserve that eligibility, the BSP fog
assignment and the existing exposed-plane/depth calculation. Opaque plants
and alpha-blended depth-writing cutouts are supported; weather and additive
effect sprites are not expanded by this batch.

The original cutout and texture-anchored distance-coverage rejection run first.
For surviving alpha-blended leaves, fold the old leaf blend followed by the
equal-depth fog overlay into one blend:

```text
result = leafRGB * leafAlpha * (1-fog)
       + background * (1-leafAlpha) * (1-fog) + fogRGB * fog
```

No extra plant draw, image readback or vertex expansion. The existing dynamic
uniform stream gains three vec4 fields (128 to 176 bytes before device alignment).
Per-volume/eye/blend parameters are cached for the frame. The default block
is zero outside this draw scope; world surfaces, models, water order, plant
density, wind and fading are unchanged. Goggles retain their local-fog bypass.

`r_vulkanVegetationFog` defaults to 1, is non-archived and not cheat protected.
With `r_vulkanLocalFog 1`, switching it to 0 isolates the plant change without
changing water or terrain fog. Neither switch changes water opacity.

## JKO launch

```sh
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_vulkanLocalFog 1 +set r_vulkanVegetationFog 1 +set r_vulkanTiming 1 \
2>&1 | tee /tmp/jko-vegetation-fog.log
```

Use a disposable devmap session, not a campaign save. Enter `devmap yavin_swamp`,
wait for loading to finish and leave the opening cinematic normally if needed.
Then enter commands individually:

```text
god
noclip
setviewpos 1300 -1650 2080 298
```

`noclip` toggles: turn it on only if currently off, and leave it on at these
inspection positions. Turn off the saber so its light does not mask the test.
Close the console during comparisons. Some head adjustment can be needed
because the game adds its camera/head offset to the teleported position.

### A. Local-fog hanging vines

From `1300 -1650 2080 298`, look ahead and slightly up at hanging plants beneath
the rock overhang, toward approximately `(1382,-1807,2110)`. BSP surface 2720
uses `textures/yavin/s_rock1_vines` and local fog 10: dark olive `swater1` fog,
depth 512. Its two vine stages are alpha-cutout, alpha-blended and depth-writing.

Compare `r_vulkanVegetationFog 0` and `1` twice, keeping `r_vulkanLocalFog 1`.
Wait about 10 seconds with the console closed in each mode. With 1 the plant
details should lose contrast and blend toward the same olive fog as their
surroundings. With 0 they retain their former, unfogged appearance. Look at
the vines, not just the rock/water: those should not change with this switch.
Leaf holes must remain transparent, not become fog-colored rectangles.

Farther comparison, looking toward the same cluster:

```text
setviewpos 1250 -1570 2080 299
```

Fog should strengthen with distance through the assigned medium, smoothly
and consistently between eyes. Translate/rotate your head and check edges,
wind motion and plant fading. Optional higher view:
`setviewpos 1300 -1650 2140 298` (look slightly down).

This authored volume has no exposed plane. It is a full-fog test, not proof
of a visible waterline intersection. Boundary cases are covered by GPU tests;
do not spend time searching for a half-submerged plant at this position.

### B. No-local-fog control

```text
setviewpos -1763 2801 2478 153
```

Look ahead/slightly up at hanging vines toward `(-1843,2841,2510)`.
Surface 1130 has global-fog membership only, not a valid local water volume.
Switching `r_vulkanVegetationFog 0/1` should not alter these plants or their
surroundings. Existing global fog is not modified by this work.

Both targets were checked for a clear BSP point and approach ray to exposed
vine geometry. Triangle centers are not reconstructed random plant anchors.
If the target is not visible, capture `viewpos` and a screenshot rather than
searching the entire level. Reproduce candidate discovery with:

```sh
python3 tools/audit_vegetation_fog.py "/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData/base"
```

### C. Regression and report

Leave both fog switches at 1. Check light-amplification goggles if available:
their fog bypass should still work, with ordinary fog returning on exit.
Check head movement/stereo comfort, stable cutouts, normal scenery/characters
and water. A brief familiar Artus sludge visit is a useful regression; JKA's
Yavin river/pool must also remain unchanged, but use a separate JKA session.

Exit normally. Report the local-fog difference, dry control, cutout/stereo
stability and any performance change. Logs include `rd-vulkan-vegetation-fog`
eligible-batch counts, first affected draw and existing timing summaries.

## Automated verification

### Focused distance follow-up (2026-10-03)

No renderer changes or deployment for this follow-up. Use the same launch
command, but log to `/tmp/jko-vegetation-distance.log` to preserve the first run.
After `devmap yavin_swamp`, enable god/noclip as above and leave
`r_vulkanLocalFog 1`. Inspect the same target around `(1382,-1807,2110)` from:

```text
setviewpos 1399 -1709 2080 260
setviewpos 1460 -1364 2080 260
```

These are approximately 100 and 450 horizontal game units from the target.
Read-only BSP collision and approach checks passed at each position and with
32/64-unit eye offsets. They are not yet headset-verified. Nominal analytic
fog amounts are roughly 0.44 and 0.94, a larger separation than the first run.
Those values describe the target fragment calculation, not measured headset
contrast or all plants visible at either position.

First compare near/far with vegetation fog on, then repeat with it off.
Close the console for each view. The same distant cluster should retain much
less leaf detail with fog on than off; nearby/intervening plants are not the
comparison target. Wind, texture filtering and existing world fog may also
change appearance, so retain screenshots from all four combinations if the
difference is unclear. No need to repeat the accepted dry-control test.
Leave both fog cvars at 1 and exit normally. Outcome: Patola confirmed clear
attenuation with distance; this follow-up passed.

### Test coverage

The real fragment shader GPU probe tests 24 combinations of opaque/alpha
leaves, eye/fragment sides of the boundary and fog depths. It compares against
the original two-pass blend and checks empty alpha/coverage holes. Existing
45 local-fog cases and fogged terrain under liquid-detail ordering also run.
Contract tests protect eligibility, one draw per batch, descriptor layout,
frame/world cache clearing and goggles bypass. These tests do not replace
headset validation or establish a measured performance improvement.

Both renderer builds succeeded; all 30 CTest tests passed. The GPU probe also
passed explicitly with `VK_LAYER_KHRONOS_validation` on the RX 7900 XTX, without
reported validation errors. Existing unrelated compiler warnings remain.
Logs: `/tmp/jkxr-vegetation-fog-build.log`, `/tmp/jkxr-vegetation-fog-tests.log`,
`/tmp/jkxr-vegetation-fog-gpu.log`.

Installed renderer hashes match the build outputs:

- JKA: `4baabcc284d72285b597ec978464266fb93d9bcae3ac46b67aae695da306dc80`
- JKO: `540a7f68a840f43af2209f4dd193955c82485a656d32d64e78379e8a0464003d`

Previous renderer pair backed up in
`OpenJK/build-vulkan-clean/pre-vegetation-fog-20261002-6K4jxu/`.
Only the two renderer libraries were deployed. No engine/game modules, assets,
saves or user configuration were replaced.
