# JKXR on Linux (native PCVR)

This fork adds a **native Linux PCVR build** of JKXR — the single-player VR
ports of **Star Wars Jedi Knight: Jedi Academy** and **Star Wars Jedi Knight
II: Jedi Outcast** — using OpenXR with the **X11/GLX** graphics binding.
Upstream JKXR only ships Windows (PCVR) and Android (standalone) builds.

Both games have been built, run and played end-to-end on Arch Linux
(GCC 16, CMake 4.x, AMD RX 7900 XTX / Mesa, WiVRn runtime): menus, motion
controllers, gameplay, in-engine and pre-rendered cutscenes, sound/music,
save/load.

You need **your own copy of the games** (Steam, GOG, original CDs). Only the
engine and VR assets are provided here.

## 1. Requirements

A C/C++ toolchain plus development packages for SDL2, OpenXR, OpenGL/GLX,
X11, zlib, libpng, libjpeg and GLU.

Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake zip \
    sdl2 openxr glu libglvnd libx11 zlib libpng libjpeg-turbo mesa
```

Debian/Ubuntu (approximate):

```sh
sudo apt install build-essential cmake zip libsdl2-dev libopenxr-dev \
    libglu1-mesa-dev libgl-dev libx11-dev zlib1g-dev libpng-dev libjpeg-dev
```

At runtime you also need:

- An **OpenXR runtime** supporting the OpenGL (`XR_KHR_opengl_enable`)
  binding: **WiVRn** (streams to standalone headsets), **Monado**, or
  **SteamVR**. Make sure it is the *active* runtime
  (`~/.config/openxr/1/active_runtime.json`) — WiVRn's dashboard and most
  runtimes set this up for you.
- An **X11 session or XWayland**. The OpenXR session uses the X11/GLX
  binding; under Wayland just run with `SDL_VIDEODRIVER=x11` (the
  instructions below include it).

## 2. Building

One command builds both games' engines, renderers, game code and packs the
VR asset `.pk3` files:

```sh
./build_linux.sh
```

(Manual CMake invocations: see the script — the project lives in
`OpenJK/`, the full cross-platform source tree.)

Artifacts:

| File | What it is |
|------|------------|
| `build-linux/openjk_sp.x86_64` | Jedi Academy SP VR engine |
| `build-linux/openjo_sp.x86_64` | Jedi Outcast SP VR engine |
| `build-linux/code/rd-vanilla/rdsp-vanilla_x86_64.so` | JKA renderer |
| `build-linux/code/rd-vanilla/rdjosp-vanilla_x86_64.so` | JKO renderer |
| `build-linux/code/game/jagamex86_64.so` | JKA game code |
| `build-linux/codeJK2/game/jospgamex86_64.so` | JKO game code |
| `assets/z_vr_assets_{base,jka,jko}.pk3` | VR assets (menus, VR weapon models, configs) |

## 3. Installing into your game directory

Find your game's **GameData** directory — the one containing `base/` with
`assets0.pk3` etc. The location varies with how you installed the game:

- Default Steam library:
  `~/.local/share/Steam/steamapps/common/Jedi Academy/GameData`
  `~/.local/share/Steam/steamapps/common/Jedi Outcast/GameData`
- Any custom Steam library, e.g.:
  `/games/SteamLibrary/steamapps/common/Jedi Academy/GameData`
- GOG/innoextract installs: wherever `GameData/base/assets0.pk3` ends up.

Then:

```sh
./install_linux.sh jka "/path/to/Jedi Academy/GameData"
./install_linux.sh jko "/path/to/Jedi Outcast/GameData"
```

### File layout (what the script does / manual install)

Binaries go in **GameData/** (the engine looks for the renderer and game
`.so` next to the executable), pk3s go in **GameData/base/**:

```
GameData/
├── openjk_sp.x86_64                  # JKA engine    (JKO: openjo_sp.x86_64)
├── rdsp-vanilla_x86_64.so            # JKA renderer  (JKO: rdjosp-vanilla_x86_64.so)
├── jagamex86_64.so                   # JKA game code (JKO: jospgamex86_64.so)
└── base/
    ├── assets0.pk3 ... assets3.pk3   # YOUR game data (from Steam/GOG)
    ├── z_vr_assets_base.pk3          # VR assets (both games need this one)
    ├── z_vr_assets_jka.pk3           # game-specific VR assets (JKO: z_vr_assets_jko.pk3)
    └── z_vr_weapons_jka_Crusty_and_Elin.pk3   # VR weapon models (JKO: ..._jko_...)
```

> The pk3s can alternatively go into the engine's per-user home path, which
> has the highest search priority and avoids touching the game directory:
> `~/.local/share/openjk/base/` for JKA, `~/.local/share/openjo/base/` for
> JKO. (This is what the Arch package launchers do.)

### Running

Start your OpenXR runtime (e.g. WiVRn dashboard + headset client), then:

```sh
cd "/path/to/Jedi Academy/GameData"
SDL_VIDEODRIVER=x11 ./openjk_sp.x86_64
```

```sh
cd "/path/to/Jedi Outcast/GameData"
SDL_VIDEODRIVER=x11 ./openjo_sp.x86_64
```

Config and savegames live in `~/.local/share/openjk/` (JKA) and
`~/.local/share/openjo/` (JKO). To run from elsewhere, pass
`+set fs_basepath "/path/to/GameData"`.

## 4. Arch Linux package (PKGBUILD)

A PKGBUILD is provided in [`packaging/arch/`](packaging/arch/):

```sh
cd packaging/arch
makepkg -si
```

It installs into the system (root-owned) paths:

- `/usr/lib/jkxr/` — engines + renderer/game `.so` modules
- `/usr/share/jkxr/{jka,jko}/` — VR asset pk3s
- `/usr/bin/jkxr-jka`, `/usr/bin/jkxr-jko` — launchers

Since your game data stays wherever Steam/GOG put it, the launchers locate
it at run time:

- They auto-detect the default Steam library
  (`~/.local/share/Steam/steamapps/common/<game>/GameData`).
- For any other location, point them at it with an environment variable:

```sh
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" jkxr-jka
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" jkxr-jko
```

(Export the variable in your shell profile to make it permanent.) The
launchers copy the VR pk3s into `~/.local/share/openjk|openjo/base` on
start — nothing is ever written to the game directory or anywhere root-owned
at run time.

## 5. Technical notes / status

- The Linux OpenXR glue lives in `JKXR/linux/` (ported from
  `JKXR/windows/`); the OpenXR session is created with
  `XrGraphicsBindingOpenGLXlibKHR` from the SDL-created GLX context.
- The release build uses `-O1 -fno-strict-aliasing`. The latter is
  **required**: this Quake-derived code type-puns through incompatible
  pointer types, which GCC's strict aliasing (enabled at `-O2`/`-O3`)
  miscompiles into crashes (e.g. in the cgame effects system). `-O1` matches
  the proven Android build as a hedge against other latent UB in this
  20-year-old codebase. The PKGBUILD also disables LTO for the same reason.
- Pre-rendered (ROQ) video cinematics — including the opening text crawls —
  are presented on the virtual screen (quad layer) rather than per-eye: the
  desktop rd-vanilla renderer lacks the stereo-replay feature the Android
  renderer uses for per-eye video. In-engine cutscenes remain fully
  immersive (`vr_immersive_cinematics`).
- Multiplayer is not built (SP VR only, same as upstream JKXR).

## Troubleshooting

- **"No VR Headset Detected" dialog** — no active OpenXR runtime, or the
  runtime doesn't expose `XR_KHR_opengl_enable`. Check
  `~/.config/openxr/1/active_runtime.json` and that the runtime is running
  (for WiVRn: server running *and* headset client connected).
- **"no current GLX context" error** — you're on native Wayland; run with
  `SDL_VIDEODRIVER=x11` (XWayland).
- **"Failed to load ... library"** — the renderer/game `.so` files are not
  next to the engine binary (or in `fs_basepath`). See the layout above.
- **Menus missing / vanilla menus shown** — the `z_vr_assets_*.pk3` files
  are not in a searched `base/` directory.
