# Polygon glass: JKO (2026-09-29)

Status: user accepted rectangular and non-rectangular glass on 2026-09-29.
The run log confirms *106, *107, *172 and *173 each emitted 20 shards with no
trap error. Console teleport follow-up remains pending headset acceptance.

## First-run follow-up

The first headset run reached `ns_streets` *107 and failed with
`Bad cgame system trap: 4294967295`. The new syscall was appended to the game
enum and dispatcher but omitted from the shared engine's JKO enum mirror and
`CL_ConvertJK2SysCall`. Add both: JKO must translate before dispatch. The native
regression test now compiles the actual converter and dispatch case, verifies
all mirrored JKO syscall IDs against the game's enum, and exercises the new
query's arguments, output buffers and result. Prior source-only checks missed
this intervening adapter; geometry tests cannot validate engine wiring.

The world-locked console also stayed at the old position after `setviewpos`.
The client now observes the player teleport bit in the snapshot reached by
render time, resets only the renderer's captured console pose, and recaptures
it during the same draw. Both pointer and render anchors move together;
prompt/history, modifiers, animation and gameplay-input ownership are retained.
Ordinary head/movement changes do not trigger this recapture. See
`console-lifecycle-audit.md` for tests.

Follow-up builds and all 24 CTest suites passed. Updated installed engines and
game DLLs match the build byte-for-byte; renderers are unchanged and still match.
Backup: `build-vulkan-clean/pre-glass-teleport-followup-20260929-EHHzLP/`.
Engine SHA-256:

```text
JKA 89fec9dd07a2d1eea5ef758847d4098b2b6084686dc1b4696d56d58e8eedbca8
JKO ad84d7961ec208d31a21b758fc7ed97451b598931f86c31a2d79217a77cf9d56
```

## Contract

The old four-corner query remains unchanged. When it cannot describe the
selected broad face, a new bounded renderer query returns its convex boundary
(3-64 vertices). A shared helper triangulates and subdivides this contour with
continuous pane UVs, preserving winding and coverage, capped at 128 shards.
The existing FX polygon primitive supplies gravity, bouncing, rotation, impact
effects and fading. Sound, damage, targets, collision removal and area portals
remain in the existing destruction flow. Rectangular panes keep their original
subdivision, randomization and physics path.

This extends the existing largest-face approximation; it is not a new volumetric
fracture simulation for every face of arbitrary compound brush models.
No global material, blending, lighting, water or input changes are involved.
No proprietary assets are changed. No new cvars or save-layout changes.

Renderer API is now 23: deploy BOTH engines, BOTH renderers and BOTH game DLLs
together. Do not combine these engines with an API-22 renderer.

## Offline verification

- Production geometry tests: capacity failures, front/back selection, winding,
  area conservation, finite continuous UVs, three plane orientations, small/large
  panes, shard cap and invalid/degenerate inputs.
- Source contract tests cover both game syscall paths and the quad fallback.
- ASan/UBSan retail BSP audit: JKA 80 quad panes; JKO 287 quad panes plus five
  polygon panes. All have a supported selected face. No uncovered panes remain.
- The five polygon candidates produce 20 shards per selected face in
  `ns_streets`, and five in `ns_hideout`.
- Full JKA/JKO build and all 24 CTest suites passed. Installed engines,
  renderers and game DLLs were compared byte-for-byte with build outputs.

Deployment backup: `OpenJK/build-vulkan-clean/pre-polygon-glass-20260929-PzVdZq/`.
Launchers synchronize each new game DLL into the per-user search path at launch.
Installed renderer SHA-256:

```text
JKA 30b24d31ce9ee8f7bbbc75e06efe6f403832f004c456ea41d6ff9b4d53d47a18
JKO b5dc036caaacf8966222edcc350d1338c5f41fd482980754e4c4673f37305e41
```

Reproduce the asset audit (read-only, requires owned data):

```sh
c++ -std=c++17 -O1 -g -fsanitize=address,undefined tools/check_glass_geometry.cpp -o /tmp/check-glass-polygon
python3 tools/audit_glass_assets.py /tmp/check-glass-polygon "/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData/base"
```

## Live run

Launch from the terminal as usual:

```sh
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jko-polygon-glass.log
```

Use console commands one at a time. `devmap` enables cheats; do not overwrite
campaign saves during this test. Coordinates below come from the retail BSP,
not a live headset traversal. Use `noclip` only for positioning, then toggle it
OFF before firing: weapons cannot be tested while noclip is active. Close the
console before firing too; it intentionally suppresses gameplay inputs.

### Nar Shaddaa Streets

```text
devmap ns_streets
god
give all
noclip
setviewpos -448 1096 100 90
noclip
```

Look up: the two sloping pentagonal panes are at x=-512..-384,
y=1056..1136, z=180..204 (models *106 and *107).
Shoot each with an ordinary blaster. Watch pieces fall and fade for about five
seconds. Move your head sideways to check stereo and world anchoring.
The second pair (*172 and *173) is at the same height farther along y:

```text
setviewpos -448 1400 100 270
```

An optional opposite-face check requires reloading (destroyed panes do not
respawn), positioning above the roof near z=260, then leaving noclip before
shooting down. Do not use it for the first acceptance check: the two positions
above already cover all four pentagonal Streets panes.

### Nar Shaddaa Hideout

```text
devmap ns_hideout
god
give all
noclip
setviewpos 465 -505 -350 225
noclip
```

The small irregular pane (*26) occupies x=388..428, y=-576..-543,
z=-352..-277. Look toward its middle around (412,-556,-315).
Shoot it and confirm pieces, break sound and no persistent floating patch.

Finally break an ordinary rectangular window and check that its shattering
still behaves normally. Unrelated world materials, models and stereo should
remain unchanged. Exit normally.

The log records `rd-vulkan-glass-polygon: inline=... vertices=5` and
`glass-polygon: boundary=5 shards=...` when a target pane takes the new path.
These diagnostics distinguish a polygon test from a nearby ordinary window.
