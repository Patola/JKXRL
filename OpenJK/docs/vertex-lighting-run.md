# Secondary vertex lighting

Implemented 2026-10-02. Headset accepted in both games: Patola tested all
listed scenes, observed the lighting differences, and confirmed expected behavior.

Optimization follow-up: JKO `artus_mine`, `setviewpos -2560 -100 665 85`,
has a notable performance cost by headset observation. It appeared unchanged
between `r_vulkanVertexStyles 0` and `1`; do not attribute it to secondary
vertex lighting without measurements. Retain this exact position and yaw for
CPU/GPU frame-time profiling in the optimization rounds, separately from the
Artus red-pulse cargo-room benchmark. No numeric timing conclusion yet.

Both renderer builds succeeded and all 29 CTest tests passed. Logs:
`/tmp/jkxr-vertex-lighting-build.log`, `/tmp/jkxr-vertex-lighting-tests.log`.
Installed renderer hashes match the build outputs:

- JKA: `ab7c45a5f9c4f686aee825a77da2ef1f44b1734c9721da185aa20ea95a425b8c`
- JKO: `87d802b983e2158253d4fa521896f860747400ad4bcbc2313f38c9d1eb717347`

Previous renderer backup: `OpenJK/build-vulkan-clean/pre-vertex-lighting-20261002-3FSdbb/`.
No engine/game modules, assets or saves were changed by this deployment.

## Scope and verification

The original ComputeFinalVertexColor combines up to four authored RGB layers
with the current light-style palette, shifts the integer sum by eight, clamps
to 255, and preserves alpha. Vulkan previously retained only layer zero.
The fix retains extra source colors only for multi-style vertex-lit BSP
surfaces (including baked scenery models and inline brushes/patches).

Patches sample each layer using the same tessellation at load time. Finalized
geometry is cached after boundary/billboard preparation. Only RGB changes at
runtime; positions, normals, UVs and original alpha are retained. Palette changes
update only affected contiguous vertex ranges before rendering the first eye;
the second eye shares that result. Each vkCmdUpdateBuffer packet is <=64 KiB,
with vertex-read/transfer barriers; failed frames invalidate the cache.
The shared vertex remains 56 bytes, with no additional draw passes. Static
styles do not upload each frame. No global palette or single-style change.

`r_vulkanVertexStyles 1` is the default. `0` restores the previous first-layer
path immediately. The toggle is non-archived and not cheat protected. It is
not a global brightness, bloom, or dynamic-light setting. Constant-color stages
can legitimately remain unchanged despite having secondary BSP color data.

`tools/check_vertex_lighting_contracts.py` compares the production combiner with
the extracted legacy routine over 20,000 randomized cases and executes the
actual sparse-update function with intercepted Vulkan calls: chunk bounds,
unchanged ranges, RGB-only writes, irrelevant/unchanged styles, exact off-mode
restoration, and failed-frame retry. These are CPU/command contract tests, not
a claim of a rendered headset test or a performance measurement.

Read-only asset audit:

```sh
python3 tools/audit_vertex_lighting.py "/games/SteamLibrary/steamapps/common/Jedi Academy/GameData/base" kor2
python3 tools/audit_vertex_lighting.py "/games/SteamLibrary/steamapps/common/Jedi Academy/GameData/base" t3_bounty
python3 tools/audit_vertex_lighting.py "/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData/base" artus_mine
```

## JKA run

```sh
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_vulkanVertexStyles 1 +set r_vulkanTiming 1 \
2>&1 | tee /tmp/jka-vertex-lighting.log
```

Use a disposable devmap session, not a campaign save. After each map finishes
loading, skip its opening cinematic normally if needed, then enter the commands
individually. `noclip` is a toggle: use it only if currently off, and keep it on
at these inspection positions to avoid falling. `god` is optional. Turn off the
saber blade so its dynamic light does not mask the authored lighting. Close the
console during each comparison; leave each mode visible for about 10 seconds.

### 1. Korriban statue

```text
devmap kor2
god
noclip
setviewpos 10765 5812 -3520 180
```

Look ahead at the nearby stone guard statue. This is BSP surface 176,
`models/map_objects/korriban/statue_guard01`, around (10669,5812,-3530).
Its base average RGB is (48,49,41); the secondary layer averages (140,65,29)
and uses style 1. Compare `r_vulkanVertexStyles 0` then `1`, twice. With 1,
the statue should gain authored warm light and follow its variation. This is
surface illumination, not an extra floating halo. Do not expect every wall
to change. Alternate statue: `setviewpos 14308 -1619 -3008 90`.

Positions are source-derived, with clear BSP eye positions and approach rays
checked, not headset-verified. Small head/position adjustments may be useful.
Do not spend time hunting if the target is not visible: record `viewpos` and
a screenshot instead.

### 2. Bounty building: multiple styles and curved geometry

```text
devmap t3_bounty
god
noclip
setviewpos 1536 640 160 90
```

Look ahead and slightly up at the opening/curved trim around (1536,800,200),
including the sides near x=1412 and x=1664. Do not shoot/break it before the
comparison. Nearby `textures/bounty/base` and `textures/factory/basic3` patches
have base lighting plus styles 34 and 43, controlled by the map's bomb-3 lights.
Initially the white style-43 lights are on; orange style-34 lights start off.
Compare 0/1: with 1, lit parts should gain the missing white contribution.
Do not require an orange pulse unless the script has activated those lights.
Move your head sideways; shading should remain attached and agree between eyes,
without geometry cracks or transparency changes.

This map has 1,907 multi-style vertex-lit surfaces (119 source patches), useful
for checking update cost. Leave 1 on and play for 30-60 seconds. Report any
hitching or unusually bright/incorrect surfaces.

## JKO run (separate session)

```sh
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_vulkanVertexStyles 1 +set r_vulkanTiming 1 \
2>&1 | tee /tmp/jko-vertex-lighting.log
```

```text
devmap artus_mine
god
noclip
setviewpos -2560 -100 665 85
```

Look ahead and down at the damaged R5 droid pieces near (-2560,0,625).
Surfaces 18743..18752 use base lighting plus style 32. This is separate from
the accepted cargo-room red pulse (style 14). Style 32 starts on in a fresh
map. Compare 0/1: the wreck's casing/legs should gain missing light with 1,
without geometry, transparency or unrelated-surface changes. Alternate clear
viewpoint: `setviewpos -2640 0 665 350`.

## Regression and report

Leave `r_vulkanVertexStyles 1` afterwards. Check head translation/rotation and
stereo agreement. A short familiar Yavin/Hoth visit should retain ordinary
colors, water, characters and saber effects. In JKO, the Artus cargo-room red
pulse should still illuminate its walls/floor normally. Save/menu/console
behavior must remain unchanged.

Exit normally. Report the statue, Bounty trim and damaged droid separately:
visible toggle differences, animation where applicable, stereo/artifacts and
performance. Logs contain `rd-vulkan-vertex-lighting` load counts/cache sizes
and toggle upload sizes; `rd-vulkan-timing` includes recording/GPU frame costs.
