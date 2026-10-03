# Forked electricity

## Accepted (2026-09-16)

User extensively tested Lightning in JKA and JKO with sabers and firearms,
looking along the casting direction, looking away, and holding the head still.
Casting-hand aiming is a pass. The historical follow-up below records the
earlier failure and the implemented correction; it no longer needs a repeat.

## Follow-up: casting-hand aiming

The first user run looked like the familiar gaze-following wide fan. Its log
does confirm new forks (1-2 branches, 9-60 segments in sampled submissions),
but branch visual acceptance remains open. Branch topology is separate from
the level-3 authored fan; do not turn a wide attack into a single narrow ray
just to make the forks easier to notice.

JKA's saber path used lerpAngles and its firearm path used viewaxis. JKO's
firearm path used torso angles. Damage already partially used controller angles
but retained cached skeletal hand positions and a body-centered target cone.
Both games now use BG_CalculateVRLightningPose for local first-person visuals
and damage: offhand ANGLES_DEFAULT pointing pose (not weapon pitch-adjusted or
saber grip pose), current world-space hand origin, and its full orientation.
The wide cone, line trace and visibility trace start at that hand. NPCs,
third-person/cinematic actors and remote cameras retain their old pose logic.
Local VR does not emit an additional animation-driven right-hand fan. No input,
Force duration/cost/damage amounts, gestures or renderer changes in this pass.

Focused retest, with the same launch command below:

1. Use level-3 Lightning with a single saber. Hold the offhand still, pointing
   at a target to one side, and turn your head elsewhere. The fan must stay
   hand-directed. In right-handed mode this is the left hand; left-handed mode
   swaps it along with existing controls.
2. Keep looking straight ahead and rotate/raise/lower the casting hand. Both
   visual origin and fan direction should follow. Confirm enemies in that
   direction take damage, rather than enemies only in your gaze direction.
   The level-3 attack retains its broad cone, so use clearly separated targets.
3. Switch to a firearm and repeat; also spot-check dual sabers if convenient.
   Release, switch powers, and confirm no stuck activation/slow motion.
4. Optional in a disposable test session: `helpusobi 1`, `setForceLightning 1`
   and `give force` test the narrow bolt. Then `setForceLightning 3` for the
   wide fan; reload the original save afterwards rather than saving alterations.
5. Exit normally. `jkxr-lightning-aim` samples casting-hand origin/angles versus
   view pitch/yaw every two seconds while active. `rd-vulkan-electricity`
   continues to report actual generated forks. Preserve `/tmp/jka.log` or
   `/tmp/jko.log` for aim/branch confirmation.

## Scope and source contract

Legacy `rd-vanilla/tr_surface.cpp::DoBoltSeg` consumes `RF_FORKED`: at most three
forks shared by the entire bolt, a 7% chance per eligible point in its first
20%, branches aimed between their origin and the grown root endpoint plus
random offsets of up to 80 units per axis. Forks inherit current thickness and
the taper flag. Both cgames map `FX_BRANCH` to `RF_FORKED` in CElectricity.
In authored .efx files, the historical flag alias `usePhysics` requests this
for Electricity; `useModel` is taper and `useBBox` is growth. This does not
mean that these particles acquire gameplay physics.

The Vulkan helper preserves its accepted jagged trunk while adding this bounded
branch topology. It deliberately does not copy legacy global/random-mutating
micro-subdivision: immutable inputs and a local unsigned seed produce identical
positions for each eye and color/bloom pass. Geometry remains at most 64
segments per strand and 256 per bolt, batched with the existing shader. There
is no new render target or per-fork draw call. Shader, RGBA, depth, combat,
controls and Force selection behavior are unchanged.

## JKA first

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka.log
```

1. Load a familiar save with Force Lightning, preferably in a dark Hoth
   corridor or `vjun2`/`vjun3`, facing several metres of open space. Use one
   saber for this focused visual test, so dual-saber modifiers do not distract.
2. Select Lightning and hold its activation several times, for about 20 seconds
   total. Look for occasional shorter tendrils splitting from the first part
   of the longer bolts. They are not guaranteed on every bolt, especially
   very short ones. Test both empty space and a nearby enemy/wall.
3. Move/turn your head while casting: branches must stay attached and stereo
   comfortable, with no eye-filling quads, detached flashes or persistent bolts
   after release. Enemy damage and selected-power release should be unchanged.
4. Check ordinary saber glow, weather if present, and frame pacing. No long
   replay of scopes is needed; an on/off scope spot-check is sufficient.
5. Exit normally. `rd-vulkan-electricity` logs an actually generated forked
   submission, at most once every five seconds, with forks/segments/seed/growth.
   These are per-submission diagnostics, not counts of unique gameplay bolts.

Optional in a disposable test session, without overwriting campaign saves:

```text
helpusobi 1
setForceLightning 3
give force
```

`give force` replenishes the force pool; repeat between bursts if needed.
For a stationary emitter instead, enter `fx play env/electricity`, then
`fx delay 250`. It spawns near the player's feet, oriented forwards. Step
back or to the side to inspect it; enter `fx stop` before leaving, then
`helpusobi 0`. This tests the same authored branch flag without enemy AI.
Force level edits last in the loaded state: reload the original save afterwards.

## JKO separately

Use the same Force Lightning test with an existing later-game save (for example
`ns_streets`), and the regular JKO launch:

```bash
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jko.log
```

The same optional commands and `effects/force/lightning[wide].efx` flags are
present in both games. Also check that unbranched ordinary beams still look
normal. Acceptance remains pending until the headset run.
