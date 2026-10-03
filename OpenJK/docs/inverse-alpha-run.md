# Inverse-alpha energy layers

2026-09-29. Next bounded visible-material correction after accepted BSP decals.

## Source contract

Legacy GL accepts these authored source/destination factors directly:

- ONE_MINUS_SRC_ALPHA / SRC_ALPHA: C = Cs*(1-As) + Cd*As.
- ONE_MINUS_SRC_ALPHA / ONE_MINUS_SRC_ALPHA: C = (Cs+Cd)*(1-As).

The same factors apply to framebuffer alpha. Vulkan previously fell back to
SRC_ALPHA / ONE_MINUS_SRC_ALPHA, reversing the first mask and giving a different
intermediate energy layer for the second pair. Preserve texture alpha, generated
stage alpha, RGB, clocks, UVs and depth state; select the correct fixed-function
Vulkan blend state. No additional draw or shader pass is needed.

Shared renderer implementation covers BSP, models and 2D pipeline selection.
Existing blend enum values remain stable; ordinary transparency is unchanged.
Logs identify actual registration with `rd-vulkan-inverse-alpha`.

## Verified use

Read-only stock BSP/shader inspection, not a claim of headset acceptance:
`python3 tools/audit_inverse_alpha_assets.py /path/to/GameData/base`.

- JKA hoth2, surface 18: textures/hoth/ion_feedtube. Bounds
  (5004,-3572,628)..(5428,-3148,848). Moving bolt layer beneath an inverse-alpha
  casing, then a separate specular layer. Specular generation is still missing
  and is deliberately not bundled into this correction.
- JKA t3_stamp, surfaces 6584/6585: textures/factory/wallliner. Two long wall
  strips, x=-3968/-4224, y=-3456..64, z=480..608. Two energy layers, inverse
  casing mask, then lightmap modulation. Large, predictable test target.
- JKA model material models/map_objects/factory/bomb_new_glow uses both pairs
  in succession. Additional definitions include an X-wing canopy and unused/MP
  test materials; do not request campaign hunts for these declarations.
- No matching stock JKO shader definitions found. Both renderers still receive
  the shared correction, but no invented JKO demonstration is required.

## Verification

Boost blend tests compare all four output channels at alpha 0/.25/.75/1 and
guard all existing blend factors. A compiled fixture runs the actual parser
branch, checking both pairs and neighboring ordinary/additive cases.
Production-fragment Vulkan raster tests cover both equations across those alpha
values. Resource and routing checks cover world/model and 2D pipelines.

Both renderers built and deployed; all 25 CTest suites pass. Installed binaries
match their build outputs byte-for-byte. Backup:
`build-vulkan-clean/pre-inverse-alpha-20260929-otFjjA/`.

- JKA SHA256: `3a5ecdd0ca252cfcec31b071e6a1b92344ca0cfc35ff58f67f10d48765a839e0`
- JKO SHA256: `fcfa835eb35662b3911c6f7157d70484171015265029ea3f4b01718bd44f931d`

2026-09-30: user accepted both factory and Hoth texture animations as perfect.
No engine, game module or asset deployment
was needed for this renderer-only correction.

## Headset run

```sh
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka-energy-layers.log
```

1. Open the spatial console and enter `devmap t3_stamp`. It closes for loading;
   skip the introduction if needed, then reopen during gameplay.
2. Enter `god`, `noclip`, then `setviewpos -3840 -1696 544 180`.
   This viewpoint is in BSP cluster 613, facing the panel's front side. Noclip
   prevents falling or being pushed away during inspection. It is a diagnostic
   viewpoint, not an in-headset-certified navigation route.
3. Close the console. Look for moving energy within a solid surrounding casing,
   not energy replacing the casing or an opaque mask hiding the animated portion.
   Move sideways and closer; check stable material alignment and agreement
   between eyes. No cvars or exaggerated brightness are required.
4. Optional second target: `devmap hoth2`, then reopen in gameplay, enter `god`,
   `noclip`, `setviewpos 5330 -3346 738 180`. This point is in cluster 542 near
   the ion feed tube. Inspect scrolling energy and the stationary tube structure.
5. Check an ordinary wall and a familiar transparent material. The accepted
   arrow fix, water, HUD and console should remain unchanged. Exit normally.

Do not overwrite campaign saves. Loading a normal save leaves the diagnostic
test route; toggle noclip off if continuing play in the test map.
