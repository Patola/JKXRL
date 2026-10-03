# Authored specular alpha

Date: 2026-09-30. Previous inverse-alpha factory/Hoth animation tests accepted.
This batch restores alphaGen lightingSpecular in existing authored stages.
JKA headset check passed 2026-09-30: the user observed the view-dependent
brightness change and confirmed energy animation/casing remain correct.
JKO headset check also passed 2026-09-30: the protocol droid demonstrated the
intended highlight clearly. User accepted the batch in both games. This live
acceptance is separate from the numerical/build checks below.

## Contract and scope

Legacy reference: code/rd-vanilla/tr_shade_calc.cpp RB_CalcSpecularAlpha.
The reflected light/view dot product is clamped below zero, raised to power 4,
clamped to 1 and truncated to a byte before vertex interpolation. Keep the
authored normal instead of silently replacing this with per-pixel PBR lighting.
World vertices use legacy lightOrigin (-960,1980,96). Model vertices use local
entity lighting direction. Each eye uses its own local camera position.

Restore this alpha only; RGB generators, texture alpha, UVs, material depth,
blend factors, geometry, culling and pass order do not change. Entity alpha is
replaced as required by this generator, not multiplied into the highlight.
No new lights, reflection probes, bloom pass or extra draws. Model light data
uses the existing dynamic UBO stream; cache entries are frame-local and shared
between eyes. Non-specular stages do not run the highlight calculation.

Auditable retail use:
`python3 tools/audit_inverse_alpha_assets.py /path/to/GameData/base --specular`.
JKA: 166 shader definitions, 141 campaign BSP surfaces. Hoth2 ion_feedtube
stage 2 is a direct live target (surface 18). Snowtrooper armor, stormtrooper
armor, protocol droids, Yavin plant models and many machinery models also
have authored stages. Shader declarations alone do not prove every listed
model appears in a particular level. JKO: 29 definitions, 11 BSP surfaces;
the ns_hideout slick-tube stage uses destination-color modulation, so its
RGB does not visibly depend on the generated alpha. Do not request that tube
as a positive visual demonstration. Protocol droids/Mark1 models are better
JKO candidates for a subsequent separate run.

Many stages are `detail`: honor r_detailtextures, do not bypass that setting.
JKA defaults that setting to 0. Explicitly enable it before startup for this
test; it is an existing latched/archived graphics option, not a new default.

## Checks

- Compiled production parser branch covers case-insensitive specular parsing
  and resets when a subsequent generator overrides it.
- Production GLSL evaluator runs on Vulkan against a separate double-precision
  legacy reference for world/model, deformed and billboard vertices, different
  viewing directions and stereo offsets. Allow one byte of rounding tolerance.
- Production fragment raster tests preserve RGB while replacing alpha and
  guard old ordinary/wave generation and inverse-alpha material composition.
- Full regression suite, both renderer builds, installed-file equality check.

Build/deployment complete: all 26 CTest suites pass (26.02 seconds), including
real Vulkan numerical and raster tests. Installed renderers equal build outputs.
Backup: `build-vulkan-clean/pre-specular-alpha-20260930-Fx8vGX/`.
JKA SHA256: `c115777e32d730153fb2b9ed4b39361163ed4447cd3a164257bdf87b4b4c59d6`.
JKO SHA256: `6f9abd26f082fdc37ccbd57f2230b171115751e162ff011523739578d21337e2`.
Logs: `/tmp/jkxr-specular-build.log`, `/tmp/jkxr-specular-tests.log`.
Engine, game modules and assets were not changed/deployed in this batch.

## JKA headset run

```sh
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_detailtextures 1 \
2>&1 | tee /tmp/jka-specular.log
```

1. Enter `devmap hoth2`. Reopen the console after loading and enter separately:
   `god`, `noclip`, `setviewpos 5330 -3346 738 180`.
   This is the same BSP-derived tube viewpoint used for the accepted animation
   check, not a new headset-verified route.
2. Close the console. Move sideways and change viewing angle. Compare
   `r_vulkanSpecularAlpha 0` (old constant-alpha fallback) and `1` (authored
   view-dependent highlight). Expect a shaped highlight, often less uniform
   brightness, not extra glowing energy or brighter overall lighting.
3. The moving energy, stationary casing and both eyes should remain correct;
   no flashing/floating textures or new opaque patches.
4. Load a familiar Hoth save and inspect snowtrooper armor from a few angles.
   Check base textures, animation, shadows, saber lighting, HUD and console.
   A short familiar Yavin foliage/water check is useful because plant detail
   stages also use the generator. No extensive campaign replay required.
5. Leave r_vulkanSpecularAlpha at 1 and exit normally. Do not overwrite campaign
   saves from the diagnostic route; turn noclip off before ordinary play.

If detail textures were previously disabled, restore that preference on a later
launch with +set r_detailtextures 0. Highlight stages marked detail then remain
disabled by design. No promise of a visible difference on every surface/angle.

## JKO headset run

Verified target: stock ext_data/npcs.cfg declares neutral `protocol` using the
protocol player model. Its model_default.skin resolves to c3po face/arm/torso/
hand/leg materials with authored specular stages. This is a controlled spawn of
a real game model, not a claim that this NPC naturally appears in kejim_post.

```sh
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set r_detailtextures 1 \
2>&1 | tee /tmp/jko-specular.log
```

1. `devmap kejim_post`; skip the intro if desired and wait for gameplay.
2. Stand on open ground, facing a clear spot roughly two metres ahead. Reopen
   the console; enter `god`, `notarget`, then `npc spawn protocol`, separately.
   The spawn command uses player-facing direction, not the console laser.
3. Close the console, step back and inspect the gold protocol droid's head,
   shoulders, torso and legs. Compare `r_vulkanSpecularAlpha 0` and `1`, circling
   slowly/changing head position. Mode 1 gives angle-dependent shine rather
   than a uniform bright coating; do not expect reflected scenery or bloom.
4. Check both eyes, animated limbs, opaque body coverage, base gold color and
   ground shadows. Nearby walls, characters, HUD and console must stay normal.
5. Leave the cvar at 1, exit normally, and do not save over campaign progress.
   Keep this run separate from JKA. No engine/renderer changes for these steps.
