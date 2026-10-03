# Artus lighting, camera housing and floor overlap

2026-09-21. JKO headset acceptance complete: cargo-room base/pulsed lighting,
multiple unobstructed camera feeds, stable floor patch, lava/sludge and Force
Lightning all passed. JKA built but was not installed for that focused run.

## Evidence and contracts

- `/dados/arturus-mines-jkxr-quest.mp4` shows the cargo room illuminated red;
  `/dados/arturus-mines-jkxrl.mp4` shows the cargo model responding but the room
  remaining dark. The stock `artus_mine.bsp` has 1,387 surfaces with lightmap
  styles `(0,32)`. Vulkan submitted only slot zero. All secondary UVs in this
  map differ by a constant atlas offset, verified at every authored vertex.
- Accumulate up to four valid lightmaps with their current RGB lightstyle
  weights, clamp, then apply lightmap gamma/material modulation. Do not add
  untextured red light after the albedo pass or multiply two lightmaps together.
  Descriptor set 2 carries three extra samplers and a dynamic uniform block.
  No vertex-stride change, eye-specific state, CPU texture reupload or extra
  lightmap draw is introduced. Different atlas handles are supported too.
  Only secondary-style batches activate it; ordinary materials use a zeroed
  identity block. Fullbright goggles bypass it. Cache parameters once per frame
  and reuse across eyes; retire descriptor references when replacing the map.
- Loader verifies constant secondary UV offsets before enabling combination.
  Non-translated coordinates emit a warning rather than sampling wrong texels.
  Additional vertex-color lightstyles remain an explicit future audit item;
  this is not a claim of every legacy lighting mode being implemented.
- `misc_camera` hides its head but spawns a separate `impcam_base.md3` at
  origin + (0,0,16), with no saved ownership link. Both cgames now suppress the
  active camera and that matching base only in its own feed, using stable game
  entity origins, not the adjusted VR view origin. Other cameras remain visible;
  switching/leaving the feed restores normal visibility automatically. No new
  save fields, camera-pose changes or control-subtitle changes.
- The circled floor uses `textures/imp_mine/trim_floor`. BSP surfaces 12526 and
  12528 overlap on z=288 within x=[-64,-16], y=[664,712]. Both are static world
  faces, with equivalent repeating base UVs and separate lightmap rectangles.
  Stage-major batching can multiply the overlapping pixels by lighting twice.
  A load-time positive-area triangle-intersection test marks overlapping
  axis-aligned planar, implicit-material surfaces for separate complete
  base/lightmap passes. Shared edges do not count. This preserves geometry,
  normal/winding, PVS and all depth-bias settings; no hardcoded map/surface IDs.
  Authored multi-stage materials (including the accepted river) are excluded.
  The user confirmed the pictured floor patch no longer flickers.

## Verification

- Both game and renderer variants build. All 22 CTest suites pass.
- Eight real-driver raster cases use the production `world.frag` on RX 7900
  XTX: pulse off/on, secondary atlas offsets, four texture/style slots, disabled
  fallback, ordinary texture isolation, gamma, saturation and material color.
  The test runs clean with `VK_LAYER_KHRONOS_validation` enabled.
- This test exposed the shader demote feature required by glslc's Vulkan 1.3
  discard output. The test and renderer now query/enable that feature explicitly.
  This is not a claim that a full engine validation run is warning-free; the
  separately recorded push-constant stage-mask issue is still outstanding.
- CPU tests cover camera association (including old saves), neighboring camera
  visibility, coplanar overlap/shared-edge/degenerate geometry and repeated
  lightmap darkening. Integration guards cover layout compatibility, reset
  boundaries, all lightmap draw branches and geometry-preserving grouping.
- JKO renderer deployed SHA256:
  `b8bf3974a7d23361f2a7fdb8fed26ee36ce21506a3a18bd8c369f34fb70b1100`.
- JKO game module deployed SHA256:
  `969ff6e0c20cb33185c7cf8fc48722c69c98b4f1aa98d8c63be951979e946f7a`.
- Both hashes match build output. Engine, assets, settings and saves unchanged.
  Launcher content comparison refreshes the per-user game module automatically.
  Backup: `build-vulkan-clean/pre-artus-lightstyles-camera/`.

## Focused JKO run

```bash
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jko-artus.log
```

1. Load the same Artus Mine save. Watch a complete cargo-room alarm cycle:
   surrounding walls/floor should illuminate red and return when the pulse ends,
   both directly if reachable and through the camera feed.
2. Switch between available security cameras. The active camera's assembly
   should no longer block the picture. Keep the world-locked monitor and readable
   centered control instructions; exit and re-enter to check state restoration.
3. Inspect the circled floor patch after the elevator while translating and
   turning the head. Check both its flicker and unnatural dark rectangular patch.
4. Briefly recheck accepted lava/slime colors/deformation, ordinary room textures,
   saber lighting/shadows, HUD and menus. Report any new performance change.
5. Exit normally. Diagnostics `rd-vulkan-lightstyles` and `rd-vulkan-coplanar`
   identify loaded multi-style surfaces and separated overlap pairs.

## Optimization location to retain

User reports a perceptible FPS cost in the Artus Mine cargo room during the red
alarm cycle. Include this exact room in the later optimization suite, both in
person and through its security camera. Use one fixed pose and compare red pulse
off/on over several complete cycles; retain CPU record/wait, GPU stereo and
WiVRn timing alongside lightstyle/material draw counts. Keep headset resolution,
refresh rate and quality settings constant. Separate first-use pipeline work
from sustained load. Do not weaken the accepted room lighting as a workaround.
Before starting that optimization phase, ask for Patola's ARM measurement details.
