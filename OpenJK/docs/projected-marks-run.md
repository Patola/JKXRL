# Projected marks: focused acceptance

**Acceptance update, 2026-09-15:** user confirms blaster marks and their fading
in both JKA and JKO. JKA Hoth left/right footprints and mounted motor cleanup
also passed. The run below is retained as a regression procedure.

Test JKA first using an existing Hoth save with a blaster. No new assets or
renderer settings are needed. These existing cvars enable marks/footprints:

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
+set cg_marks 1 +set cg_footsteps 3 \
2>&1 | tee /tmp/jka.log
```

1. Fire several single E-11 shots at a static opaque wall. Distinguish the
   lasting impact stain from the brief impact sparks/light. Move your head
   and view it from the side: it should stay on the wall in both eyes, without
   flickering, a floating rectangle or a mark visible through another wall.
2. Shoot near a wall's edge/corner and inspect the fragments. They should clip
   to receiving geometry, not hang over the edge. Grazing/perpendicular faces
   can be excluded by the original facing thresholds. Do not use moving doors,
   glass, actors or sky as proof of this static-world query.
3. Walk over snow, then look back for footprints. Also inspect an NPC's tracks
   if available. Track eligibility is material- and footstep-event-dependent;
   metal/rock need not receive footprints. Do not enable the force-everywhere
   debug setting `cg_footsteps 4` for normal acceptance.
4. Let some marks age and continue combat. They should follow the game's normal
   fade/mark-pool behavior, without a growing performance or memory problem.
5. Check accepted water, world textures, shadows, saber effects, scopes and
   console. Load a different level and confirm old marks are not left there.
6. Exit normally; retain `/tmp/jka.log`. Report missing marks separately from
   artifacts in marks that appear, and note which weapon/material was used.

The first sixteen queries report `rd-vulkan-marks` counts. Loading logs also
report CPU cache bytes. Geometry and query tests do not replace this visual run.
After JKA acceptance, JKO can be checked separately with static wall impacts in
`kejim_post` or `ns_streets`. Its footprint configuration/callers differ; don't
change cheat-protected settings solely to duplicate the JKA test.

## Hoth follow-up: handedness and mounted sound

**Passed in headset, 2026-09-15:** user confirms left and right footprints,
motor stopping after operator death, normal sound on repeated mount/aim/dismount,
and normal exit. `/tmp/jka.log` independently records successful left/right
ground traces and nonzero mark fragments for both feet, followed by normal
client/audio shutdown. A subsequent user report confirmed blaster marks and
fading in both games. The log also reports missing tutorial videos 7/8
and two `ui/main.menu` parsing warnings; these are separate from mark acceptance.

The first run reported LEFT footprints visible, RIGHT absent. Both stock
footstep materials use the same texture; the left shader mirrors U with
`tcMod transform -1 0 0 1 1 0`. The Vulkan dynamic-effect path previously
ignored that operation, making the two materials identical. It now applies
authored affine transforms to private streamed UVs, without changing positions,
winding, projection, depth bias, lightmaps, or the source batch. Color and glow
passes use the same operation. This does not yet implement the full ordered
tcMod stack across every world/model/UI path; keep that in the material audit.

This is a confirmed handedness defect, not proof that it explains an absent
foot. Bounded `jkxr-footprint` logs separate player left/right events and ground
traces; `jkxr-footprint-mark` logs separate generated mark fragment counts.
Walk a straight line on Hoth snow for about 20 seconds and inspect both tracks.
Check NPC tracks separately if possible, then retain the log after normal exit.

Mounted weapon exit now stops the aiming motor on operator death as well as
normal dismount; destruction also clears the loop. Kill the outdoor Hoth gun's
operator while it is turning, listen for the motor to stop, then verify aiming
it yourself still produces the normal sound and dismount stops it.

The reported Force gesture failure was subsequently identified as single-saber
use. No Force input changes were made: the grip+trigger gesture chord is the
dual-saber scheme, not the ordinary selected-power trigger binding.
