# Weather-linked effects acceptance

Result: user accepted the focused JKA run, confirming weather-linked effects
and no observed regression in rain, shelter, combat, stereo or performance.
The instructions below remain as a regression protocol.

Use the normal terminal launcher and existing saves. No new assets, graphics
settings or weather commands are required. Test JKA first; JKO's legacy fizz
trigger is unchanged and should receive a later regression check.

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka.log
```

## Vjun1: saber fizz

1. Load `vjun1`, ignite the saber and briefly hold its blade in view outdoors
   in the acid rain, away from walls/enemies. Look for occasional small white
   steam puffs and short flashes along the blade; listen for short sizzling
   sounds. These are intermittent, not a continuous hum or a wall-impact spark.
2. Move fully under cover with the saber. Weather fizz should stop after the
   last short-lived steam particles fade. Return outdoors and check it resumes.
3. Rain color/animation, existing acid damage outdoors and shelter protection
   should remain correct. Don't remain exposed long enough to die for this test.
4. Check ordinary saber combat, blade appearance, Force powers and console.

## T1_rail: storm flashes

1. Load an outdoor train save. Let it settle, then observe the distant fog and
   horizon for about 60-90 seconds. Thunder/bolt/flash events are randomized;
   not every thunder sound must coincide with a fog flash.
2. Look for brief brighter fog that returns to its previous color after each
   burst. This is not a white screen overlay or a new light on every nearby
   surface. Both eyes should agree, with no camera motion or stuck brightness.
3. Check that rain, mist, nearby textures and performance remain intact.
4. Load a non-storm map afterward and verify no bright fog tint carries over.
   Exit normally and retain `/tmp/jka.log`.

The original storm flicker is intentionally short and intermittent. Stop the
test if it is uncomfortable; report the effect instead of extending exposure.

## Diagnostic interpretation

- `added acidrain ... fizzChance=0.100` proves the restored query's settings,
  not successful visual FX playback. `heavyrainfog` must not dilute 0.140.
- `storm fog flash color=(0.784 0.784 0.784)` followed by `storm fog restore`
  proves the game called the override. Logs are capped at sixteen calls per
  level; missing later log lines do not mean later flashes stopped.
- No artificial outdoor camera shake, extra bloom or compulsory screen flash
  was added. The remaining weather command omissions are tracked separately.
