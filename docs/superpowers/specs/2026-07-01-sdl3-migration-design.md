# Engine SDL2 → SDL3 Migration Design

**Status:** Approved design (pending implementation plan)
**Date:** 2026-07-01
**Branch:** `sdl3-migration`
**Relationship to other work:** This is a prerequisite sub-project that
unblocks the Vulkan port. The Vulkan M0 effort is **paused** on branch
`vulkan-m0-foundation` (T1–T3 done); it resumes on SDL3-native after this
migration lands.

## Context

JKXRL's engine platform layer (`OpenJK/shared/sdl/` + a couple of other
files) is written against the **SDL2 API** — `find_package(SDL2)`,
`<SDL2/…>` includes, `SDL2::SDL2` link. On the maintainer's machine SDL2 is
actually provided by **`sdl2-compat`** (the SDL2-compatibility shim running on
top of SDL3), and **SDL3 3.4.10 is installed natively**. So the "modern
runtime" is already present; only the *API surface* the code targets is SDL2.

The driver for migrating now: the upcoming Vulkan renderer port (and the
engine's long-term "modern architecture" goal) is best built on SDL3-native —
SDL3 has cleaner Vulkan support and is the maintained line. SDL2 and SDL3
**cannot coexist in one binary**, so this is all-or-nothing: the entire
engine platform layer migrates together. VR controller input arrives via
OpenXR (not SDL), so SDL input here is keyboard/mouse/non-VR-joystick.

## Goal

Migrate the engine from the SDL2 API to **SDL3-native**, with **no regression**
to the currently-working GL VR game. Faithful 1:1 port everywhere a SDL3
equivalent exists; the audio backend (whose SDL2 callback API has no direct
SDL3 equivalent) is rewritten behind its existing engine contract.

## Non-goals

- **No SDL_Gamepad modernization** — input stays a 1:1 port of the existing
  `SDL_Joystick` API to SDL3's (renamed) joystick API. Adopting SDL_Gamepad is
  low-value here (VR controllers are OpenXR-side) and adds risk.
- **No Vulkan renderer work** — the Vulkan port is paused; it resumes after.
- **No ARM64** — x86_64 only, same as the rest of the effort.
- **No behavior changes** — gameplay, audio mix, input feel, VR presentation
  must be unchanged.

## Approach

**One sub-project, big-bang (atomic).** Because SDL2 and SDL3 cannot link
into the same process, `find_package(SDL2)`→`find_package(SDL3)` and **all**
SDL usage must migrate together. The build is therefore only green at the
final integration task; intermediate commits during the migration will not
link. (The implementation plan accounts for this — verification is at the
end, not per-file.)

**Faithful 1:1 + forced audio rewrite** (user-approved style): mechanical
rename/re-signature everywhere SDL3 has an equivalent; rewrite only the audio
backend to SDL3's device model.

## Per-file migration

SDL2 is used in 6 files. Distinct SDL-symbol counts and migration character:

| File | SDL syms | Migration |
|---|---|---|
| `shared/sdl/sdl_input.cpp` | 88 | Events: `SDL_KEYDOWN`→`SDL_EVENT_KEY_DOWN`, `SDL_MOUSEMOTION`→`SDL_EVENT_MOUSE_MOTION`, etc.; event struct field changes (`event.key.keysym.scancode/sym/mod` → SDL3's flattened `event.key.scancode/key/mod`). Joystick API renames: `SDL_JoystickOpen`→`SDL_OpenJoystick`, `SDL_JoystickGetAxis`→`SDL_GetJoystickAxis`, `SDL_JoystickNumAxes`→`SDL_GetNumJoystickAxes`, `SDL_JoystickNameForIndex`→`SDL_GetJoystickNameForID`, `SDL_NumJoysticks`→`SDL_GetNumJoysticks`, `SDL_JoystickEventState`→`SDL_SetJoystickEventsEnabled`, `SDL_JoystickUpdate`→`SDL_UpdateJoysticks`, `SDL_JoystickClose`→`SDL_CloseJoystick`, etc. |
| `shared/sdl/sdl_window.cpp` | 70 | `SDL_CreateWindow` signature/flags (SDL3 drops x,y from the basic call; flags differ), display-mode APIs, `SDL_GL_*` (GL proc-address + context), `SDL_Vulkan_*`, `SDL_CreateRGBSurfaceFrom`→`SDL_CreateSurfaceFrom`, `SDL_GetWindowFlags`, and **`SDL_GetWindowWMInfo` (deprecated → `SDL_GetWindowProperties`)**. |
| `shared/sdl/sdl_sound.cpp` | (audio) | **Rewrite** the Q3 callback driver to SDL3's audio-device API. Engine contract (`dma_t dma`, `SNDDMA_Init/GetDMAPos/Submit/BeginPainting/Shutdown/Activate`) unchanged; only the SDL internals change (format consts `AUDIO_S16SYS`→`SDL_AUDIO_S16SYS`, new `SDL_OpenAudioDevice`/pause/lock/unlock signatures). |
| `shared/sdl/sdl_qgl.h` | 1 | `SDL_GL_GetProcAddress` (signature stable in SDL3). |
| `shared/sys/sys_unix.cpp` | 4 | `SDL_Init` flags (stable), `SDL_GetTicks` (Uint32→Uint64 in SDL3), any other minor calls. |
| `JKXR/linux/TBXR_Common.cpp` | 1 | SDL window handle use — verify it does not depend on `SDL_GetWindowWMInfo` (the GL OpenXR binding uses `glXGetCurrent*`, not WMInfo). |

## Audio rewrite (the forced-rewrite piece)

`sdl_sound.cpp` is the classic Quake-III SDL audio driver: `SNDDMA_Init`
opens a device with `SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0)` +
an `SDL_AudioSpec` (freq, `AUDIO_S16SYS`/`AUDIO_U8`, channels, samples,
callback); the callback `SNDDMA_AudioCallback` copies from the engine's
`dma.buffer` (the mixer output) into SDL's stream. `SDL_PauseAudioDevice`,
`SDL_LockAudioDevice`/`SDL_UnlockAudioDevice`, `SDL_CloseAudioDevice` manage
it.

The **engine-facing contract is fixed** (`dma` buffer + the `SNDDMA_*` entry
points). SDL3's audio-device API changed (new `SDL_OpenAudioDevice` signature,
`SDL_AUDIO_*` format constants, revised pause/lock/unlock). The rewrite keeps
the callback-from-`dma.buffer` model and re-expresses it in SDL3 — verifying
exact SDL3 audio signatures against `/usr/include/SDL3/SDL_audio.h` during
implementation.

## Dependency swap

- **CMake:** `find_package(SDL2 REQUIRED CONFIG)` → `find_package(SDL3 REQUIRED CONFIG)`;
  link `SDL2::SDL2` → `SDL3::SDL3` (and `SDL2::SDL2main`→`SDL3::SDL3_main` if used).
- **Includes:** `<SDL2/SDL.h>` → `<SDL3/SDL.h>`, `<SDL2/SDL_vulkan.h>` → `<SDL3/SDL_vulkan.h>`, etc.
- **PKGBUILD:** `depends`/`makedepends` `sdl2` → `sdl3`; drop `sdl2-compat`.

## Items the plan must handle carefully

1. **`SDL_GetWindowWMInfo`** (deprecated in SDL3). Used in `sdl_window.cpp`.
   Migrate to `SDL_GetWindowProperties`. Confirm whether anything (the OpenXR
   X11 binding, GLX drawable lookup) reads the X11 window handle from it; if
   so, rework that path. (Initial read: `TBXR_Common.cpp` uses
   `glXGetCurrent*`, not WMInfo — so likely low-risk, but verify.)
2. **Audio signatures.** Verify the exact SDL3 audio-device/callback signatures
   against `/usr/include/SDL3/SDL_audio.h` when implementing; SDL3's audio API
   is the one genuine rewrite (not a rename).
3. **`SDL_CreateWindow` / window flags.** SDL3's signature and the
   fullscreen/OpenGL/Vulkan flag values differ; the existing
   `GRAPHICS_API_OPENGL` (and the paused `GRAPHICS_API_VULKAN`) paths must
   create the window correctly under SDL3.
4. **Ticks type change.** `SDL_GetTicks` returns `Uint64` in SDL3 (was
   `Uint32`); check arithmetic/casts in `sys_unix.cpp` and elsewhere.

## Verification (acceptance — no regression)

Headless here, so for this box: **build + Arch package green**. The **user**
runs the existing **GL VR game** (default `cl_renderer`, via the launchers /
Steam VR) and confirms there is **no regression**:
- audio (music, SFX, cutscene audio) plays correctly;
- keyboard / mouse / non-VR joystick input works;
- window/display + GL VR presentation (WiVRn) works;
- menus, gameplay, save/load, shadows still work.

This migration must leave the working game working.

## After this lands

Resume the Vulkan port on SDL3-native: M0 T1 (the `rd-vulkan` scaffold) is
reusable as-is; M0 T2 (SDL Vulkan window path) and T3 (SDL linkage in
`rd-vulkan`) are reworked to the SDL3 API; then T4–T6 proceed. (The paused
`vulkan-m0-foundation` branch is reference; the resumed Vulkan work branches
from the post-SDL3 `main`.)

## Risks

- **Audio correctness** — the SDL3 audio rewrite is the most likely place to
  introduce a subtle regression (clicks, wrong format, buffering). Mitigated
  by preserving the engine contract exactly and user audio verification.
- **Big-bang atomicity** — the build is broken until all files migrate; the
  plan structures tasks so the final integration task is the build-green gate.
- **`SDL_GetWindowWMInfo` / X11 handle** — if the OpenXR binding depends on it,
  that path must be reworked. Mitigated by verifying early in implementation.
- **SDL3 API drift** — SDL3 is still evolving; pin to the installed 3.4.10
  semantics, verified against the on-machine headers.
