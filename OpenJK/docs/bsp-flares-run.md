# Map-authored light flares

2026-09-24. Implementation complete; JKA and JKO headset accepted.

User passed all five JKA checks below, clearly distinguished `r_flares 0` from
`r_flares 1`, and compared against both Quest JKXR and OpenJK. This acceptance
was followed by a separate JKO `yavin_temple` run: all four requested checks
passed, including clearly distinguishable 0/1 halos, stereo, occlusion and
ordinary rendering. The newly reported temple-vine cutout defect is separate;
see `temple-vines-run.md`.

## Contract

Legacy `ParseFlare` reads center from `dsurface.lightmapOrigin` and normal from
`lightmapVecs[2]`. These `MST_FLARE` records commonly have no vertices or indices;
the Vulkan loader previously counted and skipped them. They are distinct from
the accepted four-vertex autosprite/autosprite2 surfaces and ordinary FX sprites.

`RB_SurfaceFlare` offsets the center three units along the authored normal,
uses `portalRange` (otherwise 30) as radius, scales it by distance/512 below 512
units, and clamps it to a minimum of five units. Vertex RGB is the absolute
view/normal dot product; the BSP flare color is not used by that legacy routine.
Material rgbGen const/wave still overrides vertex RGB. Stock pulse/color/texture
definitions are reused, not replaced with a generic white halo.

The Vulkan loader caches static flare records and registers their materials
before draw recording. Scene submission filters PVS/area visibility and centers
behind the shared camera, generates four vertices per eligible flare, and appends
them to the existing world/portal effect-poly snapshots. Both eyes and bloom
reuse those positions; shader batches share existing mapped streams. No new
pipeline, descriptor, shader, vertex layout, render pass or synchronous readback.
No change to world textures, water ordering, shadows, room illumination or saber
glow tuning. Flare caches clear on world teardown, including failed reloads.

**Intentional occlusion difference:** legacy `r_flares 1` synchronously reads a
single center depth pixel and then may draw a disabled-depth halo. Vulkan keeps
normal per-fragment depth testing, even when authored depthFunc is disabled or
the legacy cvar is 2. Wall-covered pixels are rejected, but partial clipping near
an occluder silhouette can differ from legacy all-or-nothing center visibility.
Check this in the headset; do not describe it as identical legacy visibility.

The existing More Video / Light Flares setting (`r_flares`, default 1, archived)
controls generated BSP flares. It does not switch ordinary FX sprites, saber
bloom or autosprite quads. `alphaGen portal` is consumed for authored flare size
only; general portal alpha, mirrors and inline/model flares remain separate
omissions. Unsupported inline records and malformed data are logged.

## Evidence and tests

Read-only installed BSP scan finds 18 flare materials across 19 JKA maps:
notably 98 records in `hoth2`, 185 in `vjun2`, 149 in `t3_rift`, 142 in
`t1_fatal`, and 162 in `t1_rail`. JKO has 11 each in `yavin_temple` and
`yavin_courtyard`. A map reference is a candidate, not proof of visibility from
every save or route. Game archives/textures are not copied into the repository.

Numerical tests cover the offset, near/far radius, minimum size, fallback,
two-sided angular response, shared geometry around a stock Hoth coordinate,
degenerate views and nonfinite inputs. Integration guards cover load-before-draw,
PVS/cvar gating, portal/main snapshots, teardown and depth-tested effect pipelines.
Existing shader/driver tests remain applicable since GPU programs are unchanged.

Both variants build, all 22 CTest suites pass, and the new numerical suite passes
1,480 assertions. Installed renderer hashes match the build outputs:

- JKA: `68851b0f23d8ed98e73df9ab0ccf2f6303372dfd300958976eae682899c03016`
- JKO: `2632d28732080211ac4eb3188467cbd0d808aded76d69e2b10f60ce5b3cdc1b6`

Backup: `build-vulkan-clean/pre-bsp-flares-20260924/`. Atomic library replacement
does not disturb a running game; restart for testing. No engine/game-module,
asset archive, saved-game or user-config deployment in this batch.

## JKA first

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka-bsp-flares.log
```

1. Load `hoth2` and inspect blue lamp halos in the installation; its
   `flare_blue_pulse` material should pulse. Alternatively use `t1_fatal` lamps
   or the crystal-lit areas of `t3_rift`.
2. At one spot compare `r_flares 0` and `r_flares 1` in the spatial console,
   closing it to inspect each result. Look for the lamp/crystal halos, not room
   brightness. Leave the value at 1.
3. Approach/retreat and move your head sideways. Halos should remain attached,
   comfortably fused and finite in size, without becoming full-eye rectangles.
4. Put a wall/doorframe between the view and a light. No halo pixels should paint
   over the solid occluder. Note any objectionable clipping at the silhouette.
5. Check ordinary geometry, actors, water, shadows, console and performance.
   Exit normally. Logs report loaded and submitted flare counts every five game
   seconds, not per-eye readbacks. No performance collectors are required yet.

With no save, `devmap hoth2` or `devmap t3_rift` supplies a disposable diagnostic
session. Do not overwrite campaign saves from it.

## JKO separately

Use the normal JKO terminal launch with `/tmp/jko-bsp-flares.log`. Check the
Yavin temple/courtyard lamp halos (`yavin_temple`, `yavin_courtyard`) with the
same 0/1, stereo and occlusion checks. JKO's separate pre-existing quad-billboard
acceptance in `kejim_post`/`artus_mine`/`ns_streets` remains pending and is not
implicitly accepted by this new batch.
