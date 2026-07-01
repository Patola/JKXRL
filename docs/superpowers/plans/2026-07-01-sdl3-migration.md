# SDL2 → SDL3 Engine Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the JKXRL engine platform layer from the SDL2 API to SDL3-native (canonical names), with the audio backend rewritten to SDL3's stream model — no regression to the working GL VR game.

**Architecture:** Atomic (big-bang) migration: SDL2 and SDL3 cannot coexist in one binary, so `find_package(SDL2)`→`find_package(SDL3)`, all `<SDL2/…>` includes→`<SDL3/…>`, and every SDL call site migrates to SDL3. The build is **intentionally red during T1–T3** and only goes green at the T4 integration gate. T1–T3 are verified by per-file compile checks against the SDL3 headers (`/usr/include/SDL3/`) + reviewer API-correctness review, not by a full link. Faithful 1:1 everywhere a SDL3 equivalent exists; audio rewritten behind its existing engine contract.

**Tech Stack:** SDL3 3.4.10 (native), CMake (`find_package(SDL3)`), Arch packaging (`sdl3`).

**Spec:** `docs/superpowers/specs/2026-07-01-sdl3-migration-design.md`

## Global Constraints

- **Faithful 1:1 + forced audio rewrite** (user-approved). No behavior changes; no regression to the GL VR game.
- **Canonical SDL3 names** — do NOT use `SDL_oldnames.h` (the SDL2-compat aliases). Migrate to the real SDL3 API.
- **Big-bang atomic** — the build is red from T1 until T4. Intermediate tasks verify via per-file `g++ -c` compile checks against `/usr/include/SDL3/SDL*.h` + reviewer code review, NOT full build/link.
- **x86_64 only. No pushing.** Local commits on `sdl3-migration` only.
- SDL3 3.4.10 installed natively; SDL3 headers at `/usr/include/SDL3/`. Verify exact signatures against those headers while implementing.
- Working directory: `/home/patola/workspace/opencode/JKXRL`. Branch: `sdl3-migration`.
- VR controller input is via OpenXR, not SDL — SDL input is keyboard/mouse/non-VR-joystick only.

## File Structure

All changes are in-place edits (no new files):

| File | SDL syms | Owner task |
|---|---|---|
| `OpenJK/shared/sdl/sdl_sound.cpp` | audio | T1 (rewrite) |
| `OpenJK/shared/sdl/sdl_window.cpp` (+ `sdl_qgl.h`) | 70 + 1 | T2 |
| `OpenJK/shared/sdl/sdl_input.cpp` | 88 | T3 |
| `OpenJK/shared/sys/sys_unix.cpp`, `OpenJK/JKXR/linux/TBXR_Common.cpp`, `OpenJK/CMakeLists.txt`, `OpenJK/code/CMakeLists.txt`, `packaging/arch/PKGBUILD` | minor + build | T4 (integration → green) |

---

### Task 1: Audio — rewrite `sdl_sound.cpp` to SDL3

**Files:** `OpenJK/shared/sdl/sdl_sound.cpp`

**Interfaces:** the engine-facing contract is FIXED and must not change: `dma_t dma`, `SNDDMA_Init(int)`, `SNDDMA_GetDMAPos(void)`, `SNDDMA_Submit(void)`, `SNDDMA_BeginPainting(void)`, `SNDDMA_Shutdown(void)`, `SNDDMA_Activate(qboolean)`. Only the SDL internals behind them change.

- [ ] **Step 1: Read the SDL3 audio API.** `rg -n 'SDL_OpenAudioDeviceStream|SDL_AudioStreamCallback|SDL_PutAudioStreamData|SDL_ResumeAudioDevice|SDL_AudioSpec|SDL_AUDIO_S16SYS|SDL_GetAudioStreamFormat|SDL_DestroyAudioStream' /usr/include/SDL3/SDL_audio.h`. Note the exact `SDL_AudioStreamCallback` signature and `SDL_OpenAudioDeviceStream` params.

- [ ] **Step 2: Flip the include** `#include <SDL.h>` → `#include <SDL3/SDL.h>` (this file uses `#include <SDL.h>`, not `<SDL2/SDL.h>`; with the SDL3 CMake include dir it resolves — confirm).

- [ ] **Step 3: Rewrite the device open (`SNDDMA_Init`).** Replace the SDL2 `SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0)` + `desired.callback` with SDL3:
  - Build an `SDL_AudioSpec desired = { freq, SDL_AUDIO_S16SYS, channels }` (for 8-bit use `SDL_AUDIO_U8`).
  - `dev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, SNDDMA_AudioCallback, NULL);` — verify the exact arg order/types from the header.
  - SDL3 devices start **paused**; resume after setup with `SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(dev))` (or the stream-device API — verify).

- [ ] **Step 4: Rewrite the callback.** SDL3's stream callback signature differs (it's `void(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount)`). Convert the existing `SNDDMA_AudioCallback` (which copies `dma.buffer` → SDL's buffer) to push the same bytes into the stream via `SDL_PutAudioStreamData(stream, ptr, len)` — preserving the exact same `dmapos`/wraparound logic from the SDL2 callback. Compute `obtained` format/channels/freq from the spec you requested (SDL3 gives you what you asked for; there's no separate `obtained`).

- [ ] **Step 5: Update the management calls.** `SDL_PauseAudioDevice`/`SDL_ResumeAudioDevice`, `SDL_LockAudioDevice`/`SDL_UnlockAudioDevice` (check whether SDL3 stream devices still have lock/unlock — if not, guard `dma.buffer` with the engine's existing locking or remove the now-unneeded calls), `SDL_CloseAudioDevice`→`SDL_DestroyAudioStream(dev)`. Adapt `SNDDMA_Submit`/`SNDDMA_BeginPainting`/`SNDDMA_Shutdown`/`SNDDMA_Activate` accordingly. The `dma.samplebits = obtained.format & 0xFF` trick must be replaced (SDL3 format is an enum, not a bits-encoded value) — set `dma.samplebits` explicitly from your requested format (16 or 8).

- [ ] **Step 6: Per-file compile check** (full link is intentionally impossible until T4):
```bash
cd OpenJK && g++ -std=c++11 -fsyntax-only -I/usr/include/SDL3 \
  -Ishared -Icode -Icode/rd-common -Ilib/gsl-lite/include \
  shared/sdl/sdl_sound.cpp
```
Expected: no errors specific to sdl_sound.cpp's SDL calls (pre-existing engine include noise is fine; the goal is the SDL3 audio calls resolve). Fix until the SDL3 audio usage is clean.

- [ ] **Step 7: Commit.** Message: `sdl3: rewrite audio backend to SDL3 OpenAudioDeviceStream (T1)`. Selective `git add`.

---

### Task 2: Window/video — migrate `sdl_window.cpp` + `sdl_qgl.h`

**Files:** `OpenJK/shared/sdl/sdl_window.cpp`, `OpenJK/shared/sdl/sdl_qgl.h`

- [ ] **Step 1: Flip includes** `<SDL2/SDL.h>`/`<SDL2/SDL_vulkan.h>` → `<SDL3/SDL.h>`/`<SDL3/SDL_vulkan.h>`.

- [ ] **Step 2: `SDL_CreateWindow`.** SDL3 signature is `SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)` (no x,y). Update all call sites: drop the x,y args (SDL2 used `SDL_WINDOWPOS_CENTERED_DISPLAY(n)` etc.); if positioning matters, follow with `SDL_SetWindowPosition`. Window flags are now `Uint64` — the existing `|=` of flag values still works but ensure no `int` truncation.

- [ ] **Step 3: `SDL_GetWindowWMInfo` (DEPRECATED/REMOVED).** Find its use (`rg -n SDL_GetWindowWMInfo OpenJK/shared/sdl/sdl_window.cpp`). Replace with `SDL_GetWindowProperties(window)` + `SDL_GetPointerProperty(props, prop_name, NULL)` to read whatever handle (X11 window, etc.) the old WMInfo call was extracting. Check what the returned `SDL_SysWMinfo` was used for and get that same handle via properties. If it fed the OpenXR GLX path, verify `TBXR_Common.cpp` doesn't actually need it (it uses `glXGetCurrent*`). If nothing meaningful needs it, remove it.

- [ ] **Step 4: Display/mode + surface calls.** Migrate `SDL_GetWindowDisplayIndex`→`SDL_GetDisplayForWindow`, `SDL_GetNumDisplayModes`/`SDL_GetDisplayMode` (SDL3 signatures changed — verify), `SDL_GetDesktopDisplayMode`, `SDL_CreateRGBSurfaceFrom`→`SDL_CreateSurfaceFrom` (params changed), `SDL_FreeSurface` (same), `SDL_GetWindowFlags` (returns Uint64 now).

- [ ] **Step 5: GL + Vulkan paths.** `SDL_GL_SetAttribute`/`SDL_GL_CreateContext`/`SDL_GL_GetProcAddress`/`SDL_GL_SwapWindow` (SDL3 signatures mostly stable — verify `SDL_GL_CreateContext` returns `SDL_GLContext`), and the `SDL_WINDOW_VULKAN`/`SDL_Vulkan_CreateSurface`/`SDL_Vulkan_GetInstanceExtensions` path added in Vulkan T2 (keep working, just SDL3 include). Preserve both the GL and Vulkan arms.

- [ ] **Step 6: Per-file compile check:**
```bash
cd OpenJK && g++ -std=c++11 -fsyntax-only -I/usr/include/SDL3 -Ishared -Icode -Icode/rd-common -Ilib/gsl-lite/include -I/usr/include shared/sdl/sdl_window.cpp 2>&1 | rg -v 'qcommon/|client/|server/' | head
```
Expected: SDL3 window/video calls resolve. Fix SDL3 API mismatches.

- [ ] **Step 7: Commit.** Message: `sdl3: migrate sdl_window.cpp + sdl_qgl.h to SDL3 (T2)`.

---

### Task 3: Input/events — migrate `sdl_input.cpp`

**Files:** `OpenJK/shared/sdl/sdl_input.cpp`

- [ ] **Step 1: Flip includes** to `<SDL3/SDL.h>`.

- [ ] **Step 2: Event types.** `SDL_KEYDOWN`→`SDL_EVENT_KEY_DOWN`, `SDL_KEYUP`→`SDL_EVENT_KEY_UP`, `SDL_MOUSEMOTION`→`SDL_EVENT_MOUSE_MOTION`, `SDL_MOUSEBUTTONDOWN`→`SDL_EVENT_MOUSE_BUTTON_DOWN`, `SDL_MOUSEBUTTONUP`→`SDL_EVENT_MOUSE_BUTTON_UP`, `SDL_MOUSEWHEEL`→`SDL_EVENT_MOUSE_WHEEL`, `SDL_QUIT`→`SDL_EVENT_QUIT` (verify all event-type constants used via `rg -n 'SDL_KEYDOWN|SDL_MOUSE|SDL_QUIT|SDL_TEXTINPUT' OpenJK/shared/sdl/sdl_input.cpp`).

- [ ] **Step 3: Keyboard event fields.** SDL3 flattened the keysym: SDL2 `event.key.keysym.scancode`/`.sym`/`.mod` → SDL3 `event.key.scancode`/`.key`/`.mod` (verify against `/usr/include/SDL3/SDL_keyboard.h` `SDL_KeyboardEvent`). `SDL_GetKeyFromScancode` now takes `(SDL_Scancode, SDL_Keymod, bool)` — update call sites.

- [ ] **Step 4: Joystick (index → SDL_JoystickID).** SDL3 is ID-based: `SDL_NumJoysticks`→`SDL_GetJoysticks(SDL_JoystickID **)` (returns an array you must `SDL_free`); `SDL_JoystickOpen(int index)`→`SDL_OpenJoystick(SDL_JoystickID)`; `SDL_JoystickGetAxis`→`SDL_GetJoystickAxis`; `SDL_JoystickGetButton`→`SDL_GetJoystickButton`; `SDL_JoystickGetHat`→`SDL_GetJoystickHat`; `SDL_JoystickGetBall`→`SDL_GetJoystickBall`; `SDL_JoystickNumAxes`→`SDL_GetNumJoystickAxes`; (same pattern for Buttons/Hats/Balls); `SDL_JoystickNameForIndex`→`SDL_GetJoystickNameForID`; `SDL_JoystickClose`→`SDL_CloseJoystick`; `SDL_JoystickUpdate`→`SDL_UpdateJoysticks`; `SDL_JoystickEventState`→`SDL_SetJoystickEventsEnabled`. The enumeration loop changes from index-based to SDL_JoystickID-based. (VR controllers are OpenXR — this is only non-VR joysticks/gamepads.)

- [ ] **Step 5: Per-file compile check:**
```bash
cd OpenJK && g++ -std=c++11 -fsyntax-only -I/usr/include/SDL3 -Ishared -Icode -Icode/rd-common -Ilib/gsl-lite/include -I/usr/include shared/sdl/sdl_input.cpp 2>&1 | rg -v 'qcommon/|client/|server/' | head
```
Expected: SDL3 input calls resolve.

- [ ] **Step 6: Commit.** Message: `sdl3: migrate sdl_input.cpp to SDL3 events/joystick (T3)`.

---

### Task 4: Minor files + CMake/PKGBUILD dependency swap → BUILD GREEN

**Files:** `OpenJK/shared/sys/sys_unix.cpp`, `OpenJK/JKXR/linux/TBXR_Common.cpp`, `OpenJK/CMakeLists.txt`, `OpenJK/code/CMakeLists.txt`, `packaging/arch/PKGBUILD`

- [ ] **Step 1: `sys_unix.cpp`** — flip include; `SDL_GetTicks` returns `Uint64` now (check any `Uint32`/`int` arithmetic/casts that would truncate — update to `Uint64`); `SDL_Init`/`SDL_Quit`/`SDL_WasInit` flags stable; any other minor calls.

- [ ] **Step 2: `TBXR_Common.cpp`** — flip its SDL include; verify it does NOT use `SDL_GetWindowWMInfo` (it uses `glXGetCurrent*`); update any SDL call it makes (1 symbol per the survey).

- [ ] **Step 3: CMake dependency swap.** In `OpenJK/code/CMakeLists.txt`: `find_package(SDL2 REQUIRED CONFIG)` → `find_package(SDL3 REQUIRED CONFIG)`; `SDL2::SDL2` → `SDL3::SDL3`; `SDL2_INCLUDE_DIRS`/`SDL2_LIBRARIES` → the SDL3 equivalents. (Note: SDL3's CMake config provides `SDL3::SDL3` and `SDL3::SDL3-shared`.) Update `OpenJK/CMakeLists.txt` too if it references SDL. Drop the `SDL2::SDL2main` if present (SDL3 merged main or provides `SDL3::SDL3_main`).

- [ ] **Step 4: PKGBUILD deps.** `packaging/arch/PKGBUILD`: `depends` `sdl2`→`sdl3`; drop `sdl2-compat`; `makedepends` likewise. (`sdl3` is in the `extra` repo, confirmed.)

- [ ] **Step 5: FULL BUILD (the green gate).**
```bash
cd /home/patola/workspace/opencode/JKXRL && rm -rf OpenJK/build-linux && ./build_linux.sh
```
Expected: configures (`find_package(SDL3)` succeeds), ALL targets build (both engines, rd-vanilla renderers, rd-vulkan stub, game code, pk3s). Fix any remaining SDL3 mismatches surfaced by the full compile. (The rd-vulkan stub from the paused Vulkan branch is NOT on this branch — this branch is off main, pre-Vulkan — so only rd-vanilla + engines build here. That's expected.)

- [ ] **Step 6: Commit.** Message: `sdl3: finish migration — sys/TBXR + CMake/PKGBUILD swap, build green (T4)`.

---

### Task 5: Build the package + USER verify no-regression

- [ ] **Step 1: Package** with the local-source trick:
```bash
cd packaging/arch
cp PKGBUILD PKGBUILD.bak
sed -i 's#git+https://github.com/Patola/JKXRL.git#git+file:///home/patola/workspace/opencode/JKXRL#' PKGBUILD
makepkg -f
```
Expected: `jkxrl-git-*.pkg.tar.zst` builds. Restore `PKGBUILD.bak` → `PKGBUILD`. Verify contents (`bsdtar -tf`) lists the engines + rd-vanilla .so + launchers + pk3s.

- [ ] **Step 2: USER VERIFY (no regression — the acceptance gate).** This is headless; YOU install the package (`sudo pacman -U jkxrl-git-*.pkg.tar.zst`) and run the **GL VR game** (default `cl_renderer`, via the launchers / Steam VR) and confirm **no regression**: audio (music/SFX/cutscenes), keyboard/mouse/joystick input, window/display + GL VR presentation (WiVRn), menus/gameplay/save-load/shadows all still work. **Do not merge until you confirm the game is fully intact.**

- [ ] **Step 3: Commit** any package-process artifacts if needed (the makepkg outputs are gitignored; likely no commit here). Note in the ledger that the user verified.

---

## Acceptance Criteria

- [ ] All SDL usage migrated to **canonical SDL3 names** (no `SDL_oldnames.h`); `<SDL3/…>` includes; CMake links `SDL3::SDL3`.
- [ ] `./build_linux.sh` green from a clean `OpenJK/build-linux/`; PKGBUILD builds a valid package with `sdl3` deps.
- [ ] Audio backend rewritten to SDL3 (`SDL_OpenAudioDeviceStream`); engine `dma`/`SNDDMA_*` contract unchanged.
- [ ] `SDL_GetWindowWMInfo` removed/replaced; `SDL_CreateWindow` SDL3 signature; joystick ID-based; `SDL_GetTicks` Uint64-safe.
- [ ] **User confirms the GL VR game is fully intact** (audio, input, window, VR, gameplay, shadows) — no regression.

## Risks & mitigations

- **Audio regression** (clicks/format/buffering): mitigated by preserving the exact `dma.buffer` copy logic + user audio verification.
- **Red build during T1–T3**: inherent to the atomic swap; mitigated by per-file `g++ -fsyntax-only` checks against SDL3 headers + reviewer API-correctness review, and the T4 full-build gate.
- **`SDL_GetWindowWMInfo` / X11 handle**: verify what consumed it; if the OpenXR binding needs the handle, rework via `SDL_GetWindowProperties` (likely low-risk — `TBXR_Common.cpp` uses `glXGetCurrent*`).
- **SDL3 signature drift**: pin to installed 3.4.10; verify every signature against `/usr/include/SDL3/` while implementing.

## After this lands

Resume Vulkan M0 on SDL3-native (new branch from this merged `main`): M0 T1 reusable; M0 T2/T3 SDL2 bits reworked to SDL3; then M0 T4–T6.
