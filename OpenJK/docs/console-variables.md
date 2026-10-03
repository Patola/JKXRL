# Console Variable Reference

This is the reference for this fork's **Linux, Vulkan, SDL3 single-player** build
of Jedi Academy and Jedi Outcast. It includes inherited variables and additions
made during this port. It is not a multiplayer guide, and an old OpenGL variable
is not automatically a working Vulkan setting.

## Find a setting

- [Alphabetical index and coverage report](cvars/README.md)
- [VR controls, gestures, comfort, haptics and console](cvars/vr.md)
- [Vulkan lighting, shadows, bloom, water and performance](cvars/vulkan.md)
- [Other graphics and effects](cvars/graphics.md)
- [HUD and camera](cvars/hud.md)
- [Gameplay and weapons](cvars/gameplay.md)
- [Audio and cinematics](cvars/audio.md)
- [Input and console](cvars/input.md)
- [Menus and customization](cvars/ui.md)
- [Engine, files and networking](cvars/engine.md)
- [Diagnostics](cvars/diagnostics.md)
- [Legacy-renderer-only names](cvars/legacy.md)

Each entry lists the literal registrations/references found in source, including
default arguments, flags, game/source scope, conditional compilation guards and
source links. Reviewed entries add explanations, units, known values and caveats.
The alphabetical index reports exactly how much behavior has been reviewed.

**Unverified means unverified:** registration does not establish purpose, a safe
range, or even that a consumer exists in the running build. Source comments are
preserved as hints, not automatically treated as authoritative explanations.
This inventory is broad; it is not a claim that every inherited variable has
already received a complete behavior audit.

## Using the console

Open the spatial console with the configured long press (default left Y for
600 ms). Both laser pointers can operate the keyboard. Short Y retains the
datapad action. The physical `~` console binding remains available.

Type a name alone to inspect its current value and default:

```text
r_vulkanBloom
vr_thermal_throw_grace_ms
```

Set a value, toggle it, or restore its registered default:

```text
r_vulkanBloom 1
toggle r_vulkanBloom
reset r_vulkanBloom
```

List registered variables in the currently running game:

```text
cvarlist
cvarlist vr_*
cvarlist r_vulkan*
cvarlist cg_*
```

`cvarlist` reports values and compact flags, but generally not explanations. It
can only list variables registered so far; some appear after loading a map,
opening a menu, or activating an effect. A name created by a configuration file
can appear even if nothing reads it. A mistyped `set` command can create such an
inert variable, so appearing in the list is not proof a feature exists.

For repeatable terminal testing, pass each setting with its own `+set`:

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set r_vulkanBloom 0 +set vr_controller_debug 1 \
2>&1 | tee /tmp/jka.log
```

For JKO use `JKXR_JKO_GAMEDATA`, its GameData path, `jkxr-jko`, and a separate
log. Game installations can be in different places; the paths above are examples.

An explicit `seta name value` requests persistence even for a normally temporary
variable. Avoid archiving diagnostics accidentally. Changing an already archived
variable normally persists through the game's usual configuration writes.
Do not use `cvar_restart` as a way to reset a single experiment: it affects many
variables. Record the old value and restore that value after a comparison.

## Defaults and flags

A **registration default** is the argument passed by source code, not necessarily
the value you will see. Multiple registrations can merge flags, preserve an
earlier default or encounter values already set by config, command line, menus,
savegames or scripts. Conditional branches can give JKA and JKO different
defaults. The generated pages deliberately show these variants rather than
guessing which one wins. Expression defaults such as `GAMEVERSION` remain
unresolved expressions.

| Source flag | `cvarlist` marker | Meaning |
| --- | --- | --- |
| `0` / `CVAR_TEMP` | none | Not archived by this registration; not automatically a debug setting. |
| `CVAR_ARCHIVE` | A | Saved through normal configuration writing. |
| `CVAR_ARCHIVE_ND` | A | Archived, but matching-default values may be omitted (`CVAR_NODEFAULT`). |
| `CVAR_LATCH` | L | Change is deferred until the responsible subsystem consumes it. |
| `CVAR_ROM` | R | Read-only through normal console modification. |
| `CVAR_INIT` | I | Initialization-only; normal later console changes are rejected. |
| `CVAR_CHEAT` | C | Protected unless the game's cheat state permits it. |
| `CVAR_SAVEGAME` | no dedicated marker | Stored with saved game state. |
| `CVAR_NORESTART` | no dedicated marker | Preserved across `cvar_restart`. |
| `CVAR_USERINFO` | U | Included in user information. |
| `CVAR_SERVERINFO` | S | Included in server information. |
| `CVAR_SYSTEMINFO` | s | Included in system information. |
| `CVAR_USER_CREATED` | ? | Created dynamically, for example by `set`. |

Other internal flags are retained verbatim in the inventory. See
[the flag definitions](../code/qcommon/q_shared.h) and
[cvar registration/console behavior](../code/qcommon/cvar.cpp).

For renderer resource settings marked latched, a full game restart is the most
reliable comparison. Not every deferred setting is applied by the same restart
command. Do not assume a successful console assignment has rebuilt GPU resources.

## Common tasks

| Goal | Settings to inspect | Important distinction |
| --- | --- | --- |
| Faster/slower turning | `vr_turn_mode`, `vr_turn_angle` | Smooth sensitivity is not directly degrees/second. |
| Fixed-gun aiming rate | `vr_mounted_yaw_speed`, `vr_mounted_pitch_speed` | Does not change physical gun pitch stops. |
| More forgiving grenade release | `vr_thermal_throw_grace_ms` | Remembers a recent stroke; no automatic safe throw. |
| Quieter saber vibration | `vr_saber_haptic_intensity` | Prefer this over weakening all haptics. |
| Console binding/animation | `vr_console_button`, `vr_console_hold_ms`, `vr_console_animation` | Panel geometry/text dimensions are not exposed by these. |
| Suppress tiny notification lines | `con_notifytime 0` | Leaves the full console usable. |
| Optional glow postprocess | `r_vulkanBloom` | Ordinary glow and dynamic receiver lighting remain with bloom off. |
| Shadow softness/darkness | `r_vulkanShadowFilter`, `r_vulkanShadowOpacity` | Filtering and darkness are separate. |
| White trooper armor response | `r_vulkanShadowWhiteArmorScale` | Does not weaken its cast ground shadow. |
| Grass visible farther away | `r_vulkanVegetationDistanceScale` | Surface-sprite fade distance, not model LOD. |
| Diagnose CPU/GPU work | `r_vulkanTiming`, `vr_controller_debug` | Neither is end-to-end WiVRn latency. |
| Headset resolution/refresh/bitrate | WiVRn/runtime settings | `vr_refresh` does not change Linux headset refresh. |

Use the in-game **More Video** settings for ordinary quality selection. In
particular, game shadow quality and the Vulkan shadow path use a combination of
variables. Old README/OpenGL explanations must not be used to infer Vulkan's
shadow algorithms. Water diagnostics and whole-image color experiments are also
not general quality presets.

## Dynamic names and exclusions

The generator scans C/C++ in `OpenJK/JKXR`, `OpenJK/code`, `OpenJK/codeJK2` and
`OpenJK/shared`, including reference-only and legacy-renderer names. It handles
literal registration calls, the existing cgame/UI cvar tables, and common
literal getter/setter calls. It does not preprocess the build or establish that
every scanned file is linked. The game labels describe source ownership, not
guaranteed runtime availability. `#if`/`#else` chains are shown unevaluated.

Computed names, such as weapon-number-dependent calibration variables, are
listed separately as unresolved expressions. Literal instances found elsewhere
still get entries. Do not infer that the listed instances exhaust a dynamically
generated family, or that a numeric weapon ID has the same meaning in both games.

Excluded: multiplayer (`codemp`), third-party libraries, `.menu`/asset scripts,
launcher settings, shipped/user configuration and arbitrary mod-created cvars.
These can set or create additional variables and override source defaults.
Environment variables such as `JKXR_JKA_GAMEDATA` are not cvars. Console commands
such as `map`, `setviewpos`, `bind` and `jkxr_itemaudit` are not cvars either.

This is a lexical source inventory plus a reviewed explanation layer, not a
claim of exhaustive runtime enumeration. Compare with `cvarlist` in each loaded
game when investigating something not found here. Never infer a setting is inert
merely because a name was computed rather than spelled literally in source.

## Maintaining the reference

From the repository root:

```bash
python3 tools/cvar_reference.py --self-test
python3 tools/cvar_reference.py --check
python3 tools/cvar_reference.py --write
```

- [reviewed.json](cvars/reviewed.json) holds source-reviewed explanations, explicit
  per-variable overrides, shared family descriptions and evidence links.
- [inventory.json](cvars/inventory.json) is the generated machine-readable source inventory.
- [cvar_reference.py](../../tools/cvar_reference.py) generates the index and category pages.

Do not manually edit generated pages. When adding or changing a cvar, trace its
consumer, update the explanation and constraints, regenerate, and run the check.
The check fails on inventory/page drift (including changed registration defaults,
flags and source locations). Regeneration lists new names as unverified until a
review is added; it does not invent an explanation or require every legacy entry
to be reviewed at once. Behavioral changes that leave the registration and
references unchanged still need a human documentation review.

The scanner tests and drift check also run through CTest when Python is available.
The generator requires only the Python standard library; no game, headset,
proprietary assets or network access is needed. It never writes game settings.
