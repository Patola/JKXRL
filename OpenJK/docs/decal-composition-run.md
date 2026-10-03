# BSP decal composition

2026-09-29. Report: t2_rancor arrow appears on a pale rectangular poster and
still flickers. `/tmp/jka-decal.log` viewpos: (-3407,4411,1156), yaw 131.

## Evidence and contract

- BSP surfaces 403 and 404 are alpha-blended `textures/rocky_ruins/arrow3` and
  `arrow3_b`, with polygonOffset and depthWrite, but no alpha test. PNG alpha
  ranges are 0..255 and 0..232 respectively; they are not opaque images.
- They lie on x-y=-8032, coincident with metal2 wall surface 10980. The wall's
  material first draws an opaque lightmap, then multiplies its texture over it.
- The previous global opaque/blended split draws the decal before the wall's
  second stage because decal surface numbers precede the wall's. Biased decal
  depth blocks that finishing stage across the entire patch, including alpha=0.
  The pale background is the unfinished wall. Marginal depth comparisons can
  expose/block the missing texture inconsistently as the viewpoint changes.
- Legacy rendering finishes the solid material before drawing a decal. Keep
  that ordering in Vulkan without stripping authored depthWrite, inventing an
  alpha cutoff or replacing texture assets. Cached index lists order finishing
  stages before polygon-offset blended decals and remaining transparency.
  Within each group BSP order is stable; maps/models without decals retain
  their previous order. Opaque/fog submission and late water paths are unchanged.
- Inline BSP models use their own cached subset, so moving doors/panels cannot
  accidentally draw the static world's list. No per-frame sort or extra draws.

## Coverage

`tools/audit_decal_assets.py` reads stock shader definitions and actual BSP
surface references in both games. JKA has 43 used blended polygon-offset
materials (380 surfaces), eight of them requesting depthWrite: arrow1_b,
arrow2/2_b, arrow3/3_b, arrow4/4_b and Rift carved_symbol. Arrow1 and exit_arrows
also receive the complete-wall ordering, as do other compatible signs, scorches,
scratches and symbols. Arrow variants occur in t2_rancor, t2_wedge and t3_bounty.
JKO contains six layered Bespin window/Cairn oil materials (604 surfaces); these do not request
depthWrite but belong after complete wall materials as well. Counts are asset
coverage, not claims that every referenced decal was visibly defective.

## Automated checks

- `tools/check_lightmap_gpu.cpp`: actual production fragment shader and blend
  state on Vulkan; old-order negative control reproduces the pale rectangle.
  New order checks zero alpha, partial alpha and foreground occlusion.
- `tools/check_lightmap_contracts.py`: compiles actual pass classifier and
  ordering function with ASan/UBSan, tests stable membership/order, inline
  subsets, implicit walls, vertex-lit promotion, solid cutouts and no decals.
  Guards static/inline draw integration and existing water/fog composition.
- Existing vines, local fog, lightstyles and full renderer tests remain gates.

## Headset acceptance (passed)

User confirms the arrow has no white background or detected flicker. Yavin
water and t1_sour tower decals remain correct. Accepted 2026-09-29.

Both renderers rebuilt and deployed; all 25 CTest suites pass. Installed files
match build outputs byte-for-byte. Engines, game modules and game assets are
unchanged in this correction. Backup:
`build-vulkan-clean/pre-decal-order-20260929-tIxdir/`.

- JKA renderer SHA256: `05c1223b79a2fa8b0f6e0f8b892475cfb076b69df9e5fe231eb6e3af6ce65eb5`
- JKO renderer SHA256: `79e0d4c6a3bc69a53de9dfc59fa3296fa08a34c5adca77b14bf96ea244da85f4`

Launch normally, without altering offset cvars:

```sh
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka-decal-fixed.log
```

1. Load the same t2_rancor save. The arrow should look painted on the wall,
   with the wall texture visible around and through its transparent portions.
2. Move/turn/lean at several distances: check both the arrow and the nearby
   previously fixed panel for flicker, with both eyes.
3. Inspect another arrow/sign if nearby; no need for a campaign-wide hunt.
4. Briefly check Yavin river/pool and t1_sour tower decals if convenient.
5. Exit normally. Report any remaining flicker separately from transparency.

Optional console navigation after loading the map: `helpusobi 1`, then
`setviewpos -3407 4411 1156 131`. The original viewpos logs the camera position,
and `setviewpos` compensates for eye height internally.
Do not overwrite campaign saves for this test.
