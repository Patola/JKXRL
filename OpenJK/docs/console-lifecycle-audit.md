# Spatial console lifecycle audit

## Teleport follow-up (2026-09-29)

Second run: the event did fire, but the console remained stranded. Both
`setviewpos` entries in `/tmp/jko-polygon-glass.log` are immediately followed by
a renderer capture. The missing contract was actual camera arrival:
`CG_InterpolatePlayerState` applied `cg_smoothPlayerPos` (default 0.5) across
the teleport. Thus the console anchored to an intermediate view, while the
camera continued converging to the destination. The previous test validated
snapshot timing but did not execute that smoothing path.

Both games now compare the previous predicted teleport bit with the new
snapshot before replacing the predicted state. On a teleport, update view
angles as usual, then return the exact new state without position/platform
smoothing. Reset the transient camera smoothing cache too, so Force Speed's
camera smoothing cannot reuse the departure position. No cvar defaults change;
ordinary walking and mover smoothing are untouched.

`check_teleport_view.py` compiles the actual interpolation functions from BOTH
games and the renderer's actual game-anchor capture. Before the fix, both
failed with camera (926,763,-26.5) instead of destination (-440,1096,125).
Afterward both pass long, short/reverse, yaw-only, pending-future and mover
teleports, with ordinary walking/platform smoothing preserved. Renderer capture
logs now include game eye and panel-center coordinates, not just physical XR
coordinates. User confirmed two successful `setviewpos` teleports on
2026-09-29: the console follows correctly. Accepted.

Both games rebuilt; all 25 CTest suites pass. The added probe first reproduced
the pre-fix intermediate anchor in both games, then passed with the correction.
Installed renderers/game DLLs match the build byte-for-byte; engines unchanged.
Backup: `build-vulkan-clean/pre-console-teleport-view-20260929-ouCbDu/`.
Camera-cache reset is excluded for scripted and remote cameras. Glass geometry,
shader behavior, cvar defaults and save layouts are unchanged.

`setviewpos` now takes an open console along, instead of leaving it behind.
Both games' `TeleportPlayer` toggle `EF_TELEPORT_BIT`; the shared client observes
that bit in the newest valid snapshot already reached by `cl.serverTime`.
It must not use a future `cl.frame` prematurely, before cgame renders arrival.
After the world draw, reset the renderer's console capture and let the following
console draw recapture its yaw-level, six-metre pose and both controller hit
anchors. Keep the console phase, scale, opacity, prompt, history, Shift/Caps
and consumed-input latches intact; discard stale hover/repeat state only.

This is tied to successful player teleports, including yaw-only `setviewpos`,
not command-text guessing or distance thresholds. Invalid commands/cheat
rejections leave the console in place. Ordinary walking/head movement does not
change its world-locked behavior. Map/save/cinematic transitions still close it.

The executable `Con_VrFollowTeleport` probe covers opening/closing, future
snapshots, both teleport-bit transitions, repeated frames, unrelated flags,
missing/overwritten snapshot slots, gameplay loss and absent renderer callbacks.
It also guards preservation of prompt/modifiers/animation and recapture of the
game/render/pointer poses together. Headset acceptance is recorded above.

## Previous acceptance

2026-09-24. Shared JKA/JKO client and server paths. User confirms map/save loads
now dismiss the console correctly. Follow-up gameplay-only activation also
passed the user's headset test on 2026-09-24.

Both engines rebuilt/deployed with matching SHA256 for the gameplay-only
follow-up; all 22 CTest suites pass. Console changes do not touch renderer/game
libraries or assets. Backup: `build-vulkan-clean/pre-console-gameplay-20260924/`.

- JKA: `fddaf21656942b5ced77a7227aff67020f50b8b8e443066aa18343e9b280cdb3`
- JKO: `a305b05a1e1f57598d7ff4662e20f1591ee47a857d312ec408093ce704d11ad1`

`Con_Close` must immediately retire both legacy and spatial state before a scene
or renderer is replaced. Normal user toggles retain their animation. Command
history, unfinished VR prompt and Caps Lock persist. Repeats/hover state stop;
consumed controller inputs stay latched until release. Renderer pose and console
mode are reset while renderer callbacks are still valid.

## Call paths

| Entry point | Close boundary | Audit result |
| --- | --- | --- |
| `map`, `devmap`, `devmapbsp`, `devmapmdl`, `devmapsnd`, `devmapall` | `SV_Map_f` -> `SV_Map_` -> `SV_SpawnServer` -> `CL_MapLoading` -> `Con_Close` | Already covered by the initial fix; no per-command patch needed. |
| `maptransition` | `SV_MapTransition_f` -> `SV_Map_` -> same spawn path | Already covered. |
| `load`, menu load, quickload | `SV_LoadGame_f` -> `SG_ReadSavegame` -> `SV_SpawnServer` | Already covered, including same-map loads. |
| `loadtransition` | `SV_TryLoadTransition` -> `SG_ReadSavegame`, or `SV_Map_` fallback | Both paths covered. |
| Client gamestate, cgame initialization | `CL_ParseGamestate`, `CL_InitCGame` | Both already close. |
| `cinematic`, `ingamecinematic` | `PlayCinematic` after successful handle acquisition, before first frame | Explicit close added here, including cached handles. System cinematics also close at the low-level entry. |
| Background videoMap/UI preview movies | Non-system `CIN_PlayCinematic` | Removed unconditional close on new video handles; these do not take ownership of the view. |
| `disconnect`, server disconnect, recoverable error, client shutdown | `CL_Disconnect` | Added close before UI/cinematic/client teardown. |
| Client memory flush | `CL_FlushMemory` | Added close before cgame/UI/renderer shutdown, including direct error-recovery callers. |
| `vid_restart`, renderer unload/shutdown | `CL_ShutdownRef` | Added close before shutdown/unload; do not wait for later cgame initialization to clear the stale phase. |
| Explicit `uimenu`, `datapad` commands | `CL_GenericMenu_f`, `CL_DataPad_f` | Close inside the existing readiness guard, immediately before menu activation. |
| Level-shot and panorama captures | `CL_GetServerCommand`, `CL_Frame` | Existing close calls use the fixed routine. |

There is no registered single-player `connect`, `reconnect` or `map_restart`
command in this client/server command table. Historical comments mentioning
`CL_Connect_f` are not evidence of an additional live route.

## Boundaries deliberately unchanged

- Missing/invalid map or save names: validation returns before the actual load
  boundary. Leave the console visible so errors can be read and corrected.
- `snd_restart`: no scene/renderer replacement in this implementation; keep the
  console available for audio diagnostics.
- Ordinary cvar commands, background menu updates and world material videos do
  not own the console. Do not close it from generic key-catcher/UI setter calls.
- Spatial activation is now gameplay-only, as requested after the user found
  long Y could open a slanted flat console over menus. Require active cgame,
  no UI catcher/fullscreen UI, no movie/pending movie, no scripted cinematic or
  security camera. Do not use `using_screen_layer`: that includes the console
  itself and would make it immediately close again. A menu/camera takeover also
  dismisses an already-open console. Closed VR drawing cannot fall back to the
  legacy disconnected/fullscreen panel.
- Remember whether a button hold started in gameplay, and revoke permission if
  gameplay ends during the hold. A blocked long hold cannot become a deferred
  opening or a short datapad tap on release. Ordinary short taps are unchanged.
  The shared open guard also applies to keyboard `toggleconsole` in VR.
- Cheat semantics are untouched. In particular, existing `SV_Map_f` can retain
  `helpUsObi` when using `map`; this audit does not change that legacy policy.

## Tests

`tools/check_vr_input_contracts.py` compiles the actual `Con_Close` body into a
small lifecycle probe with observable client/renderer state. It covers all four
phases, held/released binding, idempotence, preserved prompt/modifiers, input
latches, absent renderer hooks and flat-console behavior. Route guards verify
all map aliases, save/transition paths, close-before-teardown ordering, explicit
menus and foreground/background cinematic separation. These are automated
contract checks, not claims of complete headset acceptance.

A second compiled probe covers the actual gameplay predicate and hold-processing
code: every excluded view state, console catcher allowed, menu-to-gameplay and
gameplay-to-menu-to-gameplay holds, consumed blocked holds, and ordinary taps.

Focused headset retest: issue `map kor1` and `devmap kor1` from the spatial
console; confirm immediate dismissal and one-long-Y-press visible reopening.
Load an existing save with the console command and check the same behavior.
Use `devmap yavin1` to cover cinematic transitions. A nonexistent map name should
leave its error readable in the console. Do not overwrite campaign saves during
diagnostic map launches. Renderer-restart/disconnect paths can be checked
separately; no deliberate crash or corrupt-save test is required.
