# Wind and independent weather layers

Implemented 2026-09-13; user confirmed weather working on JKA `t1_rail`,
`hoth2` and `vjun1`. The subtle `t2_wedge` effect remains visually unconfirmed;
JKO weather still needs its separate check. Shared JKA/JKO renderer,
no new game assets, gameplay DLLs, cvars or graphics-palette changes.

## Contract and boundaries

- Commands append up to five independent rain/snow/mist layers, matching the
  legacy cloud limit. Each layer has its own particles, texture and draw batch.
  Counts are bounded at 4,000 per layer. `snow init N` and JKO `rain init N`
  now honor the supplied count; malformed commands leave existing layers intact.
- `wind`, `constantwind`, `gustingwind` and `windzone` add up to twelve wind
  regions. Constant vectors retain authored magnitude; omitted constant vectors
  default to (0,800,0). Local regions use strict interior bounds like legacy.
- Both renderer wind exports now return real state. Sum raw vectors first,
  normalize only the direction query, and report gusting above magnitude 1,000.
  This deliberately avoids legacy's inconsistent combination of an already
  normalized global direction with raw local velocities. Opposing winds cancel.
  Gameplay retains its existing outdoor/bracing/acceleration rules. The Vjun2
  `trigger_push` updraft is a different subsystem and is unchanged.
- Legacy wind targets count render updates, not milliseconds. A deterministic
  60 Hz reference clock preserves that order of duration and slew rate without
  depending on headset refresh rate, FPS, stereo replay, or gameplay RNG.
  Gust target/dead periods can last tens of seconds; constant wind remains
  active throughout. The diagnostic `gusting=1` means speed above the legacy
  threshold, not necessarily a random gust currently rising.
- Particles use persistent world positions and drag toward the local wind's
  terminal velocity. Drag integration uses elapsed game time; constant-field
  motion is tested across frame partitions. Rain/snow retain their accepted
  downward speed presets. Rain streaks align against velocity rather than
  staying vertical while moving sideways. Moving mist uses the stock
  `fog`, `heavyrainfog`, and `light_fog` texture/color/size/mass presets.
- Simulation and vertex streaming happen once per stereo frame. Fixed batch
  addresses keep the shared-eye stream cache valid. Weather is explicitly
  excluded from vegetation distance scaling. Particle simulation is CPU-side;
  draws reuse existing Vulkan pipelines. No new full-screen pass is added.
- Wrapping selects nearby world positions instead of translating every particle
  with the head. Boundary fading reduces wrap visibility. Mist fades near the
  eyes to avoid eye-filling quads. These are deliberate VR adaptations, not an
  exact replay of every legacy spawn/fade/rotation detail.
- Shelter tests use exact particle contents, not the old coarse snow cache or
  an indoor-camera veto. Rain also tests its streak tip. Existing depth testing
  clips against solid scenery. These are not swept rain/geometry collision
  simulation or a new rain-splash implementation.
- `clear` removes wind/layers while preserving the legacy outside-pain toggle;
  level reset clears everything, including pain and shelter metadata. The acid
  damage query itself is unchanged. Same timestamps do not advance wind;
  backwards game clocks rebase timing, and long stalls have bounded catch-up.
- `r_vulkanTiming 1` logs layers, configured/exposed counts, combined wind and
  CPU batch-build milliseconds every five game seconds. Texture uploads now
  happen in a separate preflight before either eye is recorded, with explicit
  `preloaded layer=...` log lines; batch-build timing excludes them. Existing renderer timing also covers
  sprite streaming/draw recording and GPU frame duration.

## JKA run

Use the usual terminal launcher, with weather enabled before loading a level:

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_weatherScale 1 +set r_vulkanTiming 1 \
2>&1 | tee /tmp/jka.log
```

Use existing saves where possible. No cheats or WiVRn statistics shortcut is
needed. The command `map LEVEL` is an alternative to loading a save, but starts
that map afresh with its normal initialization/autosave behavior.

1. **`t1_rail`, outside on the train:** spend 45-60 seconds looking forward,
   backward and sideways. The shipped map requests heavy rain and a separate
   70-particle mist layer, with constant wind speed 5,000 toward map -X.
   Look for slanted moving streaks and broader moving pale wisps at the same
   time. Mist should not replace rain or become a solid full-screen sheet.
   Briefly enter a covered car and look outside: no indoor rain, but outdoor
   weather should remain visible. Compare responsiveness in both directions.
2. **`hoth2`, outdoor starting area or a suitable save:** watch for about a
   minute, then approach cover and look back out. Authored wind is speed 3,000
   toward map -X plus variable gusts. Snow should drift sideways, remain
   stereo-stable, and stay visible outside when the camera enters shelter.
   Gradual wind changes are expected; every few seconds need not look different.
3. **Short regression in `vjun1`:** green rain remains visible, being outdoors
   causes acid damage, and shelter stops it. Then load a dry indoor save to
   verify the previous weather does not leak into the new level.

Optional additional layer-only case: **`t2_wedge` outdoors** requests 40 pale
cyan mist particles, constant speed 300 toward map +X/+Y, and variable gusts.
Look across dark walls from several metres away for slowly drifting wisps.
It does not request a rain layer. This subtle effect is not its static distance
fog. Stock BSP entity requests and all three JKA texture files were verified
directly in installed `assets*.pk3`, not inferred from map names.

Please report both visual behavior and any noticeable performance loss. Exit
normally so the log includes steady-state samples and level resets. Weather
lines can be located with:

```bash
rg 'rd-vulkan-worldfx|rd-vulkan-timing' /tmp/jka.log
```

## Subsequent JKO check

Do this separately after the JKA results. `yavin_swamp` requests ordinary
`rain init 500`; verify falling rain, sheltered views and normal swamp effects.
The JKO stock archives contain its rain texture, but not the JKA mist/snow
textures, so custom JKO maps requesting those need appropriate supplied assets.
Missing resources are reported; no proprietary textures are bundled here.

## Still separate

Storm fog-color flashes, weather-related saber fizz, surface-sprite weather
density/wind, sand/space-dust, and freeze diagnostics remain on the effects audit.
Forced outdoor camera shake needs a VR comfort decision. This batch does not
claim complete atmospheric parity or change global fog, water, shadows, shader
blending defaults, wind-driven vegetation, or the accepted Vjun2 updraft.

Verification: both renderer variants build; eight CTest suites pass, including
103 unit cases. Weather coverage is 14 cases / 12,163 assertions for exposure,
parsing, independent layers, bounded state, global/local wind, cancellation,
gust threshold, frame/eye timing, advection and anchored wrapping. These tests
do not replace the headset checks above.
