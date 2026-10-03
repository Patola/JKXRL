# JKXRL 0.6 on Linux (native PCVR)

This fork runs the single-player VR versions of Jedi Academy and Jedi Outcast
using C++17, SDL3, Vulkan 1.3/1.4 and OpenXR's Vulkan graphics binding.
The renderer uses Vulkan 1.4 where supported and otherwise requires 1.3.
Multiplayer and the former rendering backends are not supported build targets.

Both games have passed focused headset tests on Arch Linux, AMD RX 7900 XTX / Mesa,
and Quest 3 via WiVRn: controllers, cinematics/audio, combat, saves, shadows,
spatial console and selected levels. Complete campaign coverage is still pending.

You need your own legally obtained game data. The package contains the engine
and VR assets, not the original games.

## Requirements

Use a C++17 compiler, CMake, SDL3, OpenXR development headers/loader, Vulkan
headers/loader, glslangValidator, zlib, libpng and libjpeg. Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake git zip sdl3 openxr \
    vulkan-headers vulkan-icd-loader glslang shaderc zlib libpng libjpeg-turbo
```

Install the appropriate Vulkan driver for your GPU as well. Connect your headset
to an active OpenXR runtime exposing `XR_KHR_vulkan_enable2` (WiVRn is the primary
tested runtime). The session/device/swapchains belong to the Vulkan renderer;
they do not depend on a desktop graphics context. The launchers default to
SDL's X11/XWayland driver for the tested desktop input window. An explicit
`SDL_VIDEODRIVER` override is respected, but other drivers need their own tests.

## Build

```sh
./build_linux.sh
# Optional: different directory, bounded parallelism, automatic tests
BUILD_DIR=OpenJK/build-clean JOBS=12 ./build_linux.sh -DBuildTests=ON
ctest --test-dir OpenJK/build-clean --output-on-failure
```

The default build directory is `OpenJK/build-linux`. For migration from an older
checkout, use a fresh directory instead of reusing cached flags or stale modules.
The script builds both engines, both Vulkan renderers, both game modules and the
VR asset packs. Direct CMake defaults also select these single-player targets.
Old multiplayer/legacy renderer build options must be OFF.

| Artifact (relative to build directory) | Purpose |
|---|---|
| `openjk_sp.x86_64` | JKA engine |
| `openjo_sp.x86_64` | JKO engine |
| `code/rd-vulkan/rdsp-vulkan_x86_64.so` | JKA renderer |
| `code/rd-vulkan/rdjosp-vulkan_x86_64.so` | JKO renderer |
| `code/game/jagamex86_64.so` | JKA game module |
| `codeJK2/game/jospgamex86_64.so` | JKO game module |

Keep these components from the same build. The engine rejects incompatible
renderer API versions; replacing only one component is not a supported upgrade.
The engine migrates older `cl_renderer` settings to the matching Vulkan renderer.

## Manual Installation

Find the GameData folder containing `base/assets0.pk3`, then run:

```sh
./install_linux.sh jka "/path/to/Jedi Academy/GameData"
./install_linux.sh jko "/path/to/Jedi Outcast/GameData"
```

Set `BUILD_DIR` for a nondefault build directory. Engines and renderers go beside
each other in GameData. Game modules and VR pk3s go in GameData/base. If a per-user
base directory exists, the installer refreshes its game module and VR packs too,
because that directory has higher search priority. Saves and configs are retained.
Old root-level game modules are no longer the installation target.

Start the runtime and connect the headset before running:

```sh
cd "/path/to/Jedi Academy/GameData"
SDL_VIDEODRIVER=x11 ./openjk_sp.x86_64
```

Use `openjo_sp.x86_64` for JKO. From another working directory, pass
`+set fs_basepath "/path/to/GameData"`.

## Arch Package

Install the stable release from GitHub:

```sh
sudo pacman -U ./jkxrl-0.6-2-x86_64.pkg.tar.zst
```

Accept replacement of `jkxrl-git` if installed. The package name is now `jkxrl`;
launcher names, environment variables and save/config locations are unchanged.
To build the same tagged release yourself:

```sh
cd packaging/arch
makepkg -si
```

The package source is pinned to `v0.6`, not a moving development branch.
Local uncommitted changes are not included in a normal git-source package build.

Installed layout:

- `/usr/lib/jkxr/`: two engines and two Vulkan renderers.
- `/usr/lib/jkxr/base/`: two game modules.
- `/usr/share/jkxr/{jka,jko}/`: VR asset packs.
- `/usr/bin/jkxr-jka`, `/usr/bin/jkxr-jko`: launchers.

The launchers detect the default Steam library or accept explicit GameData paths:

```sh
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
    jkxr-jka 2>&1 | tee /tmp/jka.log
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
    jkxr-jko 2>&1 | tee /tmp/jko.log
```

Steam launch options remain supported:

```sh
cmd=(%command%); JKXR_JKA_GAMEDATA="$(dirname "${cmd[-1]}")" jkxr-jka
cmd=(%command%); JKXR_JKO_GAMEDATA="$(dirname "${cmd[-1]}")" jkxr-jko
```

Launchers content-compare and synchronize game modules and pk3s into
`${XDG_DATA_HOME:-$HOME/.local/share}/openjk/base` or `openjo/base` on each launch.
They do not write to the original game directory. User settings/saves remain in
these per-game home paths.

## Binary archives

The release tar.gz and zip contain the same `usr/` runtime tree as the Arch
package, plus `INSTALL.txt`. They link against current Arch shared libraries;
they are not static binaries and are not guaranteed to run on older distros.
Prefer your package manager or build from source. Do not overwrite a
package-managed installation by manually copying the archive over it.

Game data is not redistributed. Source is available at the matching Git tag;
third-party VR asset credits remain in `assets/packaged_mods_credits.txt`.

## Troubleshooting

- No headset/session: check the active OpenXR runtime and connected headset.
- SteamVR startup: the renderer requests OpenXR 1.0 plus
  `XR_KHR_vulkan_enable2`, not the installed SDK's latest core version. This
  still uses Vulkan 1.3+; OpenXR and Vulkan version numbers are independent.
  `xrCreateInstance failed: -4` in the original v0.6 package means the runtime
  rejected the SDK-derived OpenXR 1.1 request. The startup compatibility fix
  also stops renderer registration on failure instead of using an invalid
  Vulkan device. The new log identifies the runtime after instance creation.
- Missing library or renderer API mismatch: reinstall all runtime components from
  one build, then launch through the updated launcher to refresh the home module.
- Missing menus: verify that all three VR pk3s for the game are synchronized.
- No desktop keyboard focus: retain the tested `SDL_VIDEODRIVER=x11` setting.

The build retains `-fno-strict-aliasing` and conservative engine optimization.
Do not enable package-wide LTO or substitute aggressive compiler flags during
this cleanup. Performance changes are a separate, measured step.

The OpenXR startup regression test runs without a headset:
`python3 tools/check_xr_startup.py`. To probe an installed runtime explicitly,
connect its headset and use `--runtime /path/to/runtime.json`. This opt-in probe
can start the runtime; it checks instance creation only, not rendering or input.
Complete validation still requires launching both games in the headset.

## Console settings

See the [Console Variable Reference](OpenJK/docs/console-variables.md) for the
categorized source inventory, reviewed VR/Vulkan settings, flag meanings and
instructions for changing or resetting values. No headset is needed to generate
or validate that documentation.
