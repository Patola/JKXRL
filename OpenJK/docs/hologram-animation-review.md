# Cross-game hologram animation review

Prepared 2026-09-08 after the accepted Wedge performance improvement. Test one
game at a time. Keep `r_vulkanQuestColorProfile 0` for this review; profile 3 is
an experimental whole-image change requiring a restart, not a hologram preset.
Use normal terminal launch and existing saves. Performance collectors are not
required. Do not skip the cinematic sequences being inspected.

For each projection, watch at least 10-15 seconds and move your head normally.
Check temporal animation, transparency, occlusion by nearby scenery, binocular
agreement and whether it stays attached to its projector. Compare a complete
animation cycle rather than individual frames with different pulse phases.
Record map, save/location, profile and a short clip for any discrepancy.

## JKA

### t2_wedge: holographic installation

The central projected structure inside the building already used for the
performance tests. Load the same room save; the projection loops automatically.
Watch its moving contours and changing brightness from the front and side.
Check that room surfaces do not overwrite it and that dark/bright phases are
animated, not frozen blue patches. Exact Quest color matching is not assumed
for profile 0; do not change the global palette to hide a local discrepancy.

Evidence: the map places `models/map_objects/wedge/holo_map.md3` at
`(2560, 512, 338)`. It has 80 frames and uses the multi-stage
`models/map_objects/wedge/blue.tga` material in `shaders/wedge.shader`.

### t2_dpred: Dosuun Imperial firing range

Mission title: Cult Investigation - Dosuun. Use a save at the firing range,
preferably before destroying/activating its targets. Inspect the holographic
targets while intact, then shoot them as the range sequence permits. Their
layered markings should rotate slowly with transparent surroundings, not
become opaque rectangles. Check that hit-triggered target changes happen.

Evidence: actual BSP surfaces use `textures/imperial/holotarget1` and
`holotarget2`; target1 through target4 are wired to hit-triggered runners and
`scripts/t2_dpredicament/firingRangeTarget*.ibi`. The shader combines additive
layers with different rotation rates. Representative target positions are
around `(432, -2234, 79)` and `(312, -2367, 56)`.
The exact initial puzzle/activation state still needs in-game confirmation;
not all targets are promised to be active in an arbitrary mid-level save.

### t2_rogue: Coruscant penthouse area

Mission title: Capture Crime Lord - Coruscant. Inspect the holographic fixture
in Lannik Racto's penthouse area near the end of the mission. This is an
asset-confirmed location, not yet a headset-confirmed identification of the
specific fixture; a screenshot on reaching that area will establish it.
Check appearance, any animation, transparency and depth, without assuming it
has Wedge's contour animation.

Evidence: a real world-model triangle surface uses
`models/map_objects/mp/holo` near `(1208, 2208, 757)`, close to waypoint
`penthouse_10`. This is not merely an unused multiplayer asset name.

### vjun2: small holographic panels in the castle interior

Look for a pair of small holographic target/panels in the upper interior area
with Imperial officers. Use a save before breaking them. The layered markings
should rotate in opposite directions; inspect while intact and then check
their removal when broken. These are small effect panels, not character
projections. The precise room/route still needs headset confirmation, so leave
this for the campaign if the location is not immediately recognizable.

Evidence: actual BSP surfaces on breakable brush models *18/*19 use
`textures/imperial/holotarget2_small`, centered near `(506, 1792, 5088)` and
`(518, 2000, 5088)`. Two additive glow layers rotate at different rates.
Nearby officers stand around `(580, 1780, 5016)` and `(448, 1992, 5016)`.
Coordinates here are audit references, not a request to teleport through
scripted progression or bypass cheat protection.

## JKO

### kejim_post: opening Mon Mothma briefing

Start a new game and let the ship briefing play completely. Inspect the front,
idle/talking and rear views, including the final shutoff. Only one projection
should be visible at a time, correctly floating over the panel. Check the rear
camera clearance, transparency without a black rectangle, stereo comfort and
disappearance when the conversation ends.

Evidence: the map's scripted usable brushes reference
`textures/video/mon_mothma`, `_idle` and `_back` and the fade-in/out targets.
`scripts/cinematics/cinematic1.ibi` switches the talking, idle and rear
projections and triggers hologram on/off sounds. The Mon Mothma NPC in a later
cinematic is not itself evidence of another video hologram.

## Coverage limits

- Inventory examined shipped base PK3 shader/model/video names, single-player
  BSP shader/entity references, and relevant compiled cinematic script strings.
  For the additional JKA candidates, actual surface use was verified, not just
  presence in the map's shader table. Wedge and Mon Mothma are already visually
  located; the Coruscant/Vjun fixtures and Dosuun puzzle state need confirmation.
- JKO defines Morgan video materials, but this scan did not establish a
  corresponding single-player BSP use. Do not ask for a speculative Morgan
  hologram test solely because those assets exist.
- Multiplayer holograms, glowing holocron collectibles, reused hologram sounds,
  and ordinary NPCs are not automatically part of this projection checklist.
- Arbitrarily named or runtime-created scripted effects, mod overrides and
  additional campaign discoveries can extend this list. It is not a guarantee
  that every hologram in both games has been found.

Status: performance follow-up inventory prepared; headset review pending.
