# Building JKXR natively on Linux

JKXR upstream ships only Windows (PCVR) and Android (standalone) builds. This
fork adds a **native Linux PCVR build** of the single-player VR engine, using
the OpenXR **X11/GLX** graphics binding. It has been built and linked on Arch
Linux (GCC 16, CMake 4.x); see the notes at the bottom about runtime testing.

Only the Jedi **Academy** single-player engine is wired up so far (Jedi Outcast
/ `JK2_MODE` is a follow-up — see *Status* below).

## 1. Dependencies

You need a C/C++ toolchain plus the development packages for SDL2, OpenXR,
OpenGL/GLX, X11, zlib, libpng, libjpeg and OpenAL.

On Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake \
    sdl2 openxr openal zlib libpng libjpeg-turbo \
    libglvnd libx11 mesa
```

You also need a working **OpenXR runtime** that supports the OpenGL
(`XR_KHR_opengl_enable`) graphics binding, e.g. one of:

- **Monado** (`monado`) — open-source, runs on the desktop.
- **WiVRn** (`wivrn`) — streams to a standalone headset (Quest, Pico, …).
- **SteamVR** — proprietary; OpenGL support via its OpenXR runtime.

> The X11/GLX binding needs an X11 session. Under Wayland, run the game through
> **XWayland** (the default when `SDL_VIDEODRIVER` is left unset and X11 is
> available). A native-Wayland EGL binding is not implemented.

## 2. Configure & build

The CMake project lives in `Projects/Android/jni/OpenJK`. Build only the
single-player VR engine, renderer and gamecode (multiplayer / JK2 are off):

```sh
cd Projects/Android/jni/OpenJK

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release \
    -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF \
    -DBuildMPGame=OFF -DBuildMPCGame=OFF -DBuildMPUI=OFF \
    -DBuildSPEngine=ON -DBuildSPGame=ON -DBuildSPRdVanilla=ON \
    -DBuildJK2SPEngine=OFF -DBuildJK2SPGame=OFF -DBuildJK2SPRdVanilla=OFF \
    -DBuildTests=OFF

cmake --build build-linux -j"$(nproc)"
```

This produces three artifacts:

| File | What it is |
|------|------------|
| `build-linux/openjk_sp.x86_64` | the single-player VR engine executable |
| `build-linux/code/rd-vanilla/rdsp-vanilla_x86_64.so` | the renderer |
| `build-linux/code/game/jagamex86_64.so` | the Jedi Academy game code |

## 3. Installing & running

JKXR needs the original **Jedi Academy** game data plus this fork's VR asset
`.pk3`s. Lay out a game directory like:

```
jkxr/
├── openjk_sp.x86_64               # from build-linux/
├── rdsp-vanilla_x86_64.so         # from build-linux/code/rd-vanilla/
├── jagamex86_64.so                # from build-linux/code/game/
└── base/
    ├── assets0.pk3 … assets3.pk3  # from a Jedi Academy install (GOG/Steam)
    └── z_vr_assets_*.pk3          # JKXR VR assets (see z_vr_assets_* dirs in this repo)
```

The engine looks for the renderer/game `.so` next to the executable and under
`fs_basepath`, so keeping them alongside the binary (or passing
`+set fs_basepath /path/to/jkxr`) works.

Start your OpenXR runtime first (e.g. `monado-service`, or the WiVRn server +
headset client), then:

```sh
cd jkxr
./openjk_sp.x86_64                 # add +set fs_basepath <dir> if running elsewhere
```

If no headset/runtime is detected the engine shows a dialog and exits.

## Status / notes

- **Working and runtime-verified.** Jedi Academy has been played in VR on Arch
  (GCC 16, CMake 4.x, AMD RX 7900 XTX / Mesa, WiVRn runtime) through multiple
  levels — input, config, smooth turning, sound/music and save/load all work.
- The release build uses `-O1` and `-fno-strict-aliasing`. The latter is
  **required**: this Quake-derived code type-puns through incompatible pointer
  types, which GCC's strict aliasing (on at `-O2`/`-O3`) miscompiles into a
  crash in the cgame effects system. `-O1` matches the proven Android build as a
  hedge against other latent undefined behaviour in this 20-year-old code.
- **Known issue:** pre-rendered (ROQ) video cutscenes render with swapped /
  too-close eyes. In-engine 3D scenes and the 2D menu are correct; only the
  video-cinematic path is affected. Cosmetic.
- **Jedi Outcast (`JK2_MODE`)** is not yet built for Linux — only the Academy
  SP engine is wired up. The `codeJK2` / JK2 SP CMake options would need the
  same treatment.
- The Linux OpenXR glue lives in `JKXR/linux/` (ported from `JKXR/windows/`),
  differing only in the graphics binding (`XrGraphicsBindingOpenGLXlibKHR` via
  GLX instead of the Win32/WGL binding) and the millisecond clock.
