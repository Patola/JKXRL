# Goggles and scoped fog

## Accepted in both games (2026-09-16)

User verified electrobinoculars, light-amplification goggles, Tenloss and E-11
scopes in JKA and JKO, including supported zoom in/out and battery depletion.
All passed. One JKA log was accidentally truncated; acceptance is based on the
reported headset tests, not a claim to have inspected that missing log. The
pending items in the historical follow-up below are now closed.

## JKA follow-up (2026-09-16)

- User confirms light-amplification goggles work as expected. Depleted charge
  disables amplification but retains the equipped overlay/hidden weapon until
  toggled off, matching the existing game logic. JKO was tested subsequently.
- Electrobinoculars changed fog but did not magnify. The log confirms FOV*64
  requests from 5120 down to 192 (80 to 3 degrees), while Vulkan only recognized
  Tenloss artwork as optical zoom. Binocular `gfx/2d/binMask` now explicitly
  enables the same tangent-ratio magnification, equally on both projection axes.
  This frame-local flag resets each BeginFrame and does not classify goggles or
  Force Sense as zoom. Rifle-specific reticle alignment/aspect remains separate.
- Recheck binocular magnification and fog together in JKA Hoth. Toggle off and
  check ordinary view, amber goggles (no zoom), E-11 and Tenloss. This shared
  renderer correction also applies to JKO, whose fog behavior is unchanged.
  Headset acceptance of binocular magnification was subsequently confirmed.

## Contract and boundaries

- `tr_cmds.cpp::RE_LAGoggles` requests fullbright lightmaps/entity lighting and
  special amber fog (0.75, 0.42..0.445, 0.07), depth 10000. Both games call it
  only for zoom mode 3 with battery charge. Vulkan retains that gate and uses
  deterministic scene-time variation, not independent eye random samples.
- The override is snapshotted into a world submission. ClearScene clears the
  goggles request; BeginFrame clears transient goggles/range requests; world
  destruction clears both pending and submitted state. Portal state swaps with
  its own view. Menu submissions never acquire the goggles flag.
- Baked-world lighting RGB is bypassed; texture/vertex alpha, cutouts, entity
  color, geometry and UVs stay intact. Skinning lighting uses the original
  fullbright ambient/directed values. No global brightness cvar or texture
  upload changes. Special fog reuses the existing geometry-fog path and mask
  reuses binocular UV mapping. No new render target or readback is needed.
- JKA `cg_view.cpp` sends FOV*64 to SetRangedFog. The original `tr_shade.cpp`
  computes linear start/end from depthForOpaque and map distanceCull. Negative
  authored linFogStart is restored on release. Ordinary non-ranged fog keeps
  the already accepted Vulkan curve; this pass does not rewrite global fog,
  local volume rendering, weather, or lighting-grid sampling. The original JKO
  cgame has no SetRangedFog call; do not invent one for parity.
- Parsing fog worldspawn keys must own the key string because COM_ParseExt
  reuses its token buffer. The older grid-size reader has a separate token
  lifetime issue worth auditing later with lighting comparisons; not changed
  here to avoid an unrelated global lighting change.

## JKA first

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka.log
```

1. Load an existing outdoor `hoth2` save with a Tenloss. Choose a fixed distant
   terrain feature or fixture visible through fog. Compare ordinary view,
   weak zoom and strong zoom. Increasing zoom should reduce intervening fog;
   unscoping should immediately restore normal fog. The reticle remains round,
   binocularly fused and aimed correctly. Check the E-11 still works too.
2. In a dim interior, enter `la_zoom` in the spatial console, then close the
   console. This existing client command toggles light-amplification goggles;
   no cheat setting is needed. With battery charge, walls and actors should
   brighten, with the goggles artwork and warm fog tint. No magnification is
   intended. Turn your head and inspect vegetation/cutouts if available.
3. Toggle off with `la_zoom`: ordinary lighting/fog should return. Repeat a few
   times; open the menu/console and then resume. A load to another level should
   not inherit stale brightness or fog. Check Force Sense and both scopes.
4. If batteries are empty, a mask without amplification is intentional. For
   a separate cheat-enabled test session only, `devmap hoth2` starts a fresh
   map and `give batteries` refills charge. Do not overwrite campaign saves.
   `give batteries 0` can verify that depletion disables amplification while
   goggles remain selected. Refill or toggle off before ordinary testing.
5. Exit normally and preserve `/tmp/jka.log`. `rd-vulkan-view-fog` reports map
   settings and throttled goggles/range/start/end values. Shipped hoth2 has
   depth 1800 and distanceCull 1850: FOV 80 gives start 16, FOV 5 gives start
   1530. These are game-unit ranges, not headset metres.

## JKO separately

```bash
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jko.log
```

Use a dark interior in `kejim_base` or `artus_mine`, with battery charge.
Repeat goggles on/off, head movement, depleted-battery behavior if practical,
and menu/load restoration. JKO scopes should retain their accepted behavior;
the new FOV-dependent fog request belongs to JKA only. No need to replay a
whole campaign for this pass. Report visual changes and comfort separately.

## Automated coverage

Five C++ cases cover ordinary/no-fog behavior, zoom ranges including Hoth's
actual values, designer restoration, deterministic tint and scene/menu/frame/
level isolation. Source wiring guards cover both exports, battery gating,
portal swaps, RGB-only overrides and fog start/end shader inputs. Existing
weather guards verify that flashes still feed both surface fog and background.
Both renderer variants and GLSL compile; all 14 CTest suites passed. Headset
acceptance subsequently confirmed mask comfort and brightness in both games.
An additional optical-zoom case checks identity on exit, monotonic magnification,
the tangent ratio, invalid inputs and bounds. Source guards ensure the binocular
flag resets each frame and shares projection scaling without sharing rifle UI.
