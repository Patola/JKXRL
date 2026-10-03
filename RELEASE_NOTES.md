# JKXRL v0.6 - Native Linux VR, Vulkan and SDL3

JKXRL 0.6 is the public playable Linux x86-64 release of Jedi Academy and
Jedi Outcast single-player VR. It replaces this fork's former OpenGL backend
with Vulkan, uses SDL3 and C++17, and incorporates the extensively tested VR,
gameplay, presentation and effects work from the development branch.

## Highlights

- Vulkan soft shadows, dynamic lighting, optional bloom and restored material effects.
- Tracked sabers/firearms, scopes and optical devices; mounted guns and AT-ST aiming.
- Haptics, improved force gestures, dual-saber input and more forgiving grenade throws.
- Corrected cinematics/audio, holograms, sky composition, water, local fog and weather.
- Improved vegetation visibility, decals, glass, animated scenery and model attachments.
- World-locked console with controller lasers, full virtual keyboard and persistent input.
- Reliable save selection, previews and menu transitions, plus many campaign-specific fixes.
- Player-facing identity is now **JKXRL 0.6**, while preserving upstream credits.

## Install on Arch Linux

```sh
sudo pacman -U ./jkxrl-0.6-1-x86_64.pkg.tar.zst
```

Accept replacement of the conflicting older `jkxrl-git` package if installed.
Use an up-to-date Arch installation with a compatible GPU driver and a running
OpenXR runtime. Connect the headset, then launch from a terminal:

```sh
JKXR_JKA_GAMEDATA="/path/to/Jedi Academy/GameData" jkxr-jka
JKXR_JKO_GAMEDATA="/path/to/Jedi Outcast/GameData" jkxr-jko
```

Existing launcher names and environment variables remain supported intentionally.
Saves/configs remain in the existing openjk/openjo user-data directories. Back
them up before upgrading. Install the complete package: do not mix old OpenGL
engines, modules or asset packs with this release.

Both original games must be owned separately. Commercial game data is not
included. Vulkan 1.3+ and an OpenXR runtime with `XR_KHR_vulkan_enable2` are
required. Primary validation: Arch Linux, Mesa/RX 7900 XTX, Quest 3 via WiVRn.

## Downloads and verification

- `jkxrl-0.6-1-x86_64.pkg.tar.zst`: recommended Arch package, both games included.
- `jkxrl-0.6-linux-x86_64.tar.gz` / `.zip`: equivalent `usr/` runtime tree and
  installation instructions. These use current Arch shared libraries, not a
  universal static runtime; build from source on incompatible distributions.
- `SHA256SUMS`: verify downloaded artifacts with `sha256sum -c SHA256SUMS`.
- GitHub's source archives and tag `v0.6`: matching source and build recipe.

## Scope and next releases

Extensive targeted headset testing has passed in both games. Full campaign
playthroughs and broader GPU/runtime coverage continue; bug reports are welcome.
Report game/map, steps, headset/runtime, GPU/driver and logs. Multiplayer,
Windows and native ARM64 are not supported by this package. Optional bloom is
off by default. Unused/dormant effects are not inserted into campaign maps yet.

Next: fixes and measured optimization through 0.6.x/0.7.x; first native ARM64
attempt at 0.8; validated Steam Frame gameplay for 1.0; selected unused-effect
enhancements through 1.2. See [ROADMAP.md](https://github.com/Patola/JKXRL/blob/v0.6/ROADMAP.md).

Thanks to Team Beef, OpenJK, Raven Software and the asset creators. Linux,
Vulkan/SDL3 port and additional features by Patola with ChatGPT Codex assistance.
