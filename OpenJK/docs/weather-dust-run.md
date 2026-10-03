# Sand and space-dust weather

Implemented 2026-09-27; controlled JKA headset test accepted 2026-09-29.
User reports every stage behaved as described, including mixed layers and
restoration of authored weather on reload. JKO fallback visuals remain untested.
Shared JKA/JKO renderer.
No new cvars, campaign scripts, full-screen passes or assets.

2026-09-29 scope decision: the successful controlled JKA test is sufficient for
now. Further campaign-trigger searches, Droid Recovery atmosphere comparisons
and JKO fallback matching are deferred until AFTER optimized ARM 1.0 on Steam
Frame. Keep the supported presets; do not add artificial campaign weather.
The instructions below record the completed test, not a request to repeat it.

## Contract

- Restore legacy `sand` and `spacedust N` weather commands. Commands append
  independent layers, capped at five layers and 4000 particles per layer.
  Missing/invalid space-dust counts are rejected without changing current state.
- Sand: 400 amber clouds, RGB (0.9,0.6,0), alpha 0.5, zero gravity,
  mass 10-30, full billboard size 140x140 (legacy half-extents 70), height
  range +/-150. Alpha blending, no depth writes; shared-view back-to-front
  sorting is reused for both eyes. Near/wrap/distance fading affects alpha,
  not RGB, preventing dark squares at cloud edges.
- Space dust: additive RGB 0.75*0.75, zero gravity, mass 10-30, full size
  2.4x2.4, volume +/-1500 on all axes. Distance fade 1200-1500 replaces the
  old frame-count fade. No wind means floating stationary particles, not snow.
- Both use persistent world positions, existing wind integration, per-particle
  outdoor exposure, shared-eye billboard streaming and ordinary depth testing.
  Boundary/near fading is a VR adaptation, not an exact replay of legacy
  cloud rotation/fade/spawn behavior. No artificial camera shake is introduced.
- Legacy classifies space dust as water with gravity zero. It causes no fizz
  alone, but participates in the averaged fizz query when mixed with rain.
  Sand does not participate and cannot enable acid damage.
- JKA retail textures: gfx/effects/alpha_smoke2b.tga and snowpuff1.tga.
  JKO retail lacks both; fall back to gfx/effects/alpha_smoke2.tga and
  gfx/effects/whiteflare.jpg respectively (the latter is a small soft speck;
  JKO's large sparse dust.jpg atlas is unsuitable). Exact JKA texture appearance is not claimed
  for those JKO fallbacks. Preflight logs report the texture actually selected.
- Existing rain/snow/mist defaults, colors, extents and blend modes are unchanged.
  Shader/color/fog/water logic is untouched. Layer state resets on map load.

## Controlled JKA Test

Campaign follow-up (2026-09-29): user recalls blowing sand outdoors in Droid
Recovery. Retail English MENUS.str identifies this as t1_surprise. Inspection
of its BSP entities and map-local scripts found no sand weather request. Its
shader table and surfaces do contain textures/common/dark_dust: ten type-2
patches, surfaces 29-38. The stock common.shader definition uses a clamped
gradient, additive blending and constant RGB 0.141176, with no tcMod, vertex
deformation or surface sprites. This is a distinct static dust-overlay path,
not proof that the user's remembered moving effect is absent. Do not conflate
the lack of a confirmed sand-preset trigger with absence of sand/dust visuals
in the original. A natural comparison, without injected r_we weather, would be
needed before changing campaign atmosphere, but the user has now explicitly
deferred that investigation until after optimized ARM/Steam Frame 1.0.

Retail BSP entity inspection found no confirmed stock sand/space-dust weather
trigger. Do not claim a level should naturally have these effects. Use the
restored legacy renderer command `r_we` in a known outdoor area instead.
It is diagnostic, not an archived setting; no cheats are required. Do not save
this synthetic weather setup. Reload the save afterward to restore the map.

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_vulkanTiming 1 2>&1 | tee /tmp/jka-weather-dust.log
```

1. Load an outdoor Hoth save (snow confirms the location is weather-exposed).
   From the spatial console, run each line separately:

   ```text
   r_we clear
   r_we constantwind ( 300 0 0 )
   r_we sand
   ```

   Look across dark rocks/walls for 20-30 seconds. Expect translucent amber
   clouds drifting sideways, not white rain, opaque rectangles or a screen tint.
   Translate/turn your head: no eye disagreement or head-attached particles.
   Approach shelter; outdoor clouds should stay outside. Ordinary scenery,
   weapons, HUD and responsiveness should remain intact.
2. Clear the synthetic weather and test dust without wind:

   ```text
   r_we clear
   r_we spacedust 2000
   ```

   Tiny pale specks should float without falling. Move sideways and compare
   parallax against scenery. Then `r_we constantwind ( 300 0 0 )` should make
   them drift. Inspect against dark geometry, not bright snow or sky.
3. Add `r_we sand` without clearing: both tiny particles and broad clouds should
   remain present. No haze rectangle should obscure the HUD or appear through
   solid walls. This mixed test is synthetic, not a stock campaign scene.
4. Reload the Hoth save: normal authored snow/wind returns, sand/dust gone.
   Briefly check normal snow/shelter behavior, then exit normally.

`r_we clear` removes clouds and wind but intentionally retains the legacy
outside-pain toggle. Use Hoth, not Vjun acid rain, for this diagnostic. To
restore a map's authored weather, reload it rather than only using clear.
No JKO test needed in the same run; its asset fallbacks need separate visual
acceptance later. The renderer is deployed for both games.

## Verification

2026-09-28: both renderer targets build; all 23 CTest suites pass. Installed
renderer SHA-256 hashes match their build outputs:

- JKA: `951a8824554f90889c296520dfd019bb4583fd0406dcf6e759eef16f6482cf42`.
- JKO: `2d94958300e55576e720a023f86255d09fb09e42604fbe5b438f8fd5f49a77c7`.
- Previous pair: `OpenJK/build-vulkan-clean/pre-dust-weather-20260927-5T0cJ8/`.

- Unit tests: preset colors/blending/size/mass, count bounds and malformed
  commands, zero gravity, wind movement, fizz contract, unchanged old presets.
- Source guards: shared-frame build, shelter query, depth-write exclusion,
  dynamic blend selection, alpha envelope, shared-view sort and reset paths;
  r_we command registration/removal and argument forwarding.
- Builds and general renderer/GPU regression tests do not replace headset
  inspection of particle appearance, mixed blending or performance.
