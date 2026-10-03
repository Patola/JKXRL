# Camera-facing material quads

2026-09-22 implementation; JKA `kor1` headset acceptance on 2026-09-24.
User confirms fixed endpoints, correct textures/lighting, stereo agreement and
the intended 0/1 orientation difference, with ordinary rendering intact. JKO
remains pending. `/tmp/jka-autosprite.log` confirms 38 static chains loaded;
four additional chain surfaces are outside the supported static-quad subset
and explicitly warned (8394, 8395, 8581, 8582). Do not claim full inline coverage.

The run exposed a separate console lifecycle bug: `devmap` called `Con_Close`
but left the spatial phase active. The shared close routine now also ends the
spatial animation/input ownership and invalidates the renderer pose immediately.
Prompt/history and Caps Lock persist; keyboard repeat stops; held controller
inputs stay consumed until release. A held console binding cannot reopen it or
emit a datapad tap across the transition. Normal user-toggle animation is unchanged.
Map and movie callers already use this routine. A compiled production-function
probe covers all four phases, held/released binding, repeated closure, absent
renderer hooks, prompt retention and legacy flat-console behavior. Headset
verification of this follow-up is pending in both games.

Console follow-up deployment (2026-09-24): both engines rebuilt and installed;
all 22 CTest suites pass. Renderer/game libraries and assets unchanged. Backup:
`build-vulkan-clean/pre-console-transition-20260924/`. Verified engine hashes:

- JKA: `e6529e3fceec517ee118e519af9a50d13a753f7b56f7c7383de531902141c1cb`
- JKO: `192bee7fffb50ef1f4ddcb15ba9114858cbb4f341991aad0bc76055ba04d843f`

Retest: run `devmap kor1` from the spatial console. It should dismiss before
loading/cinematics, leave gameplay input available, and reopen visibly with a
single long Y press after loading. Repeat with `devmap yavin1` for the opening
cinematic sequence. Normal toggling must still preserve an unfinished command.
Do not overwrite campaign saves in cheat-enabled diagnostic sessions.

## Contract and scope

- Follow legacy `AutospriteDeform`/`Autosprite2Deform` in `tr_shade_calc.cpp`:
  autosprite constructs a square about its authored center; autosprite2 preserves
  the two shortest-edge midpoints and widths while pivoting around the long axis.
  Autosprite regenerates canonical base UVs and first-vertex color. Autosprite2
  retains its authored UVs. Both retain lightmap coordinates.
- Upload original positions once. One affine matrix per quad/shared camera basis
  per frame drives every texture, lightmap, fog and dynamic-light pass. Never
  orient separately for the two eyes. No per-frame vertex rewrite or widening
  of the 56-byte world/animated-model vertex stream. The existing deformation
  uniform grows from 160 to 224 bytes, still within the device-aligned allocation.
- Expand quad visibility bounds; retain map PVS. Keep each transformed quad in
  its own indirect group so it cannot borrow another quad's matrix. Camera-facing
  surfaces are excluded from static projected-mark clipping. Ordinary geometry,
  palette, depth bias, water order and global culling are unchanged.
- Accept only static-world affine four-vertex/six-index quads with no mixed
  wave/bulge/move operations. Flat 3x3 BSP patches reduce to four vertices through
  existing tessellation. Unsupported topology is logged, not guessed. Looking
  directly along a chain falls back to its authored short-edge direction instead
  of producing a collapsed or invalid quad.
- This does not generate zero-vertex `MST_FLARE` records. Those account for many
  JKA flare materials and are a separate next audit/implementation. Inline/model
  billboards and mixed deformation order remain outside this small batch.

## Asset evidence and verification

Read-only installed BSP inspection finds 42 chain quads in `kor1` and six in
`kor2`, all `textures/korriban/chain`. Their second pass uses a lightmap and equal
depth, making them useful coverage tests. Actual chain vertex orders at large
coordinates are included in numerical tests.

JKO `textures/imp_mine/light` has 16 flat 3x3 patches in `kejim_post` and 12 in
`artus_mine`; all control points follow a flat bilinear grid. Its authored
sawtooth pulse remains unchanged. These are existing blinking lights, not new
room illumination. `ns_streets` also has colored flare patches.

CPU tests cover center/radius, fixed endpoints, strip width, both vertex orders,
large coordinates, shared-eye transforms, parallel view and invalid geometry.
The real-driver deformation probe exercises the production GLSL matrix path
alongside all accepted wave/bulge/move cases. Source guards check upload ordering,
pass coverage, cache reset and indirect-group isolation.

Both renderer variants built; all 22 CTest suites pass. The production shader
passed 11,264 deformation samples on RX 7900 XTX with Vulkan validation enabled
and no reported validation errors (maximum CPU/GPU difference 3.815e-6).
This does not replace a headset run or claim whole-engine validation coverage.

Both renderer libraries installed in `/usr/lib/jkxr`, hashes matched to build:

- JKA: `6384412ed1be8d1f4bae4edbc1accd3dfaac55ccb37b0f7410e638152b7435eb`
- JKO: `eecffac4ce1acc220a539ae4b8fe2f2f25be281faee64f52f66d75af080c105c`

Backup: `build-vulkan-clean/pre-static-autosprites-20260922/`. No engine/game
module, asset package, save or configuration deployment in this batch. The JKA
renderer also receives the shared Artus lightstyle changes already accepted in
JKO; its cgame camera-housing change has not been deployed by this renderer-only
batch. JKO retains its already accepted camera-housing game module.

## JKA first

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka-autosprite.log
```

1. Load a Korriban save (`kor1` or `kor2`) and find hanging chain strips. Move
   sideways and look around them. They should remain anchored at both ends,
   retain their lighting/texture and not vanish edge-on or disagree between eyes.
2. At the same spot compare `r_vulkanAutosprites 0` and `r_vulkanAutosprites 1`.
   Zero leaves their authored flat orientation; one lets them face you while
   retaining their long axis. The difference is orientation, not extra chains.
   Leave it at 1. The diagnostic is not saved to configuration.
3. Check ordinary scenery, saber lighting, character animation, water, shadows
   and console. Exit normally. No timing tools are needed for this visual pass.

Without a suitable save, `devmap kor1` provides a disposable cheat-enabled run.
The stock start is near (384,656,-976); early chain quads include centers
(0,-32,-784) and (640,-32,-784). Use `noclip` to inspect if necessary, then turn
it off. Do not overwrite campaign saves during this diagnostic run.

## JKO separately

```bash
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jko-autosprite.log
```

Check existing blinking light patches in `kejim_post` or `artus_mine` from
oblique views, or colored light flares in `ns_streets`. Compare 0/1 at a fixed
location, then translate/rotate the head. Their centers and pulse timing should
stay fixed while the small quad faces the shared camera. No wall penetration,
eye disagreement or changes to the accepted cargo-room alarm/camera/floor fixes.
Exit normally. Logs report each supported surface and the total quad count;
warnings identify unsupported geometry explicitly.

The Artus red-pulse room is recorded in `artus-lighting-camera-run.md` for the
later optimization phase. Ask for Patola's ARM measurement constraints first.
