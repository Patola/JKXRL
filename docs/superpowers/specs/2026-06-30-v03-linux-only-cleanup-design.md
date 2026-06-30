# v0.3 — Linux-Only Cleanup (Remove Windows & Android Targets)

**Status:** Approved design (pending implementation plan)
**Date:** 2026-06-30
**Sub-project:** A of two (B = Vulkan port, separate cycle)

## Context

JKXRL is a VR port of Jedi Outcast / Jedi Academy, originally built for
Windows (PCVR) and Android (Quest/Pico standalone), recently ported to native
Linux PCVR using OpenXR with the X11/GLX binding. The Linux port is proven
end-to-end (menus, controllers, gameplay, cutscenes, save/load).

The Windows and Android targets are no longer needed. This change removes them
and leaves a clean, Linux-only codebase. It makes one structural improvement —
relocating the engine source out of the leftover Android NDK layout — and tags
the result `v0.3`.

A separate, later effort (Sub-project B) will assess and plan a Vulkan renderer
port for the upcoming ARM64 Steam Frame headset; that work is explicitly out of
scope here and is best done against this cleaned-up tree.

## Goals

1. Remove all Windows and Android build targets and their scaffolding.
2. Relocate the engine source to a sensible, self-documenting top-level path.
3. Keep the proven Linux build, package and launch flow working unchanged in
   behavior.
4. Preserve `code/rd-gles/` as dead reference (shader-based stereo renderer
   that informs the future Vulkan work).
5. Produce a clean `v0.3` tag.

## Non-goals

- No engine/renderer/gameplay behavior changes. The shadow levels, VR features,
  cutscene handling, etc. are untouched.
- `rd-gles` is **not** compiled — kept only as reference.
- No Vulkan work in this sub-project.
- No pushing of commits/tags to the remote (local commit + annotated tag only,
  unless the user explicitly asks to push).

## Design

### Source relocation

The engine source moves from the Android NDK layout to the repo root:

```
Projects/Android/jni/OpenJK/   -->   OpenJK/
```

`OpenJK/` is chosen because it matches the upstream project name and is
self-documenting. The CMake source root becomes `OpenJK/`; build output goes to
`OpenJK/build-linux/`.

The move uses `git mv` so history is preserved. Every reference to the old path
is updated (see "Path reference updates" below).

### Deletion / preservation manifest

**Relocate (preserve) before deleting the container:**

| Asset | From | To | Reason |
|-------|------|----|--------|
| `z_vr_weapons_jka_Crusty_and_Elin.pk3` | `JKXR-PCVR-Installer/JKA/base/` | `assets/weapons/` | Game asset, referenced by install script + PKGBUILD |
| `z_vr_weapons_jko_Crusty_and_Elin.pk3` | `JKXR-PCVR-Installer/JKO/base/` | `assets/weapons/` | Game asset, referenced by install script + PKGBUILD |

**Delete entirely (Windows + Android scaffolding):**

- `Projects/Android/` — the gradle project, **after** `git mv` of `jni/OpenJK/`
  to `OpenJK/`. This also removes `jni/{OpenXR-SDK, SupportLibs, Android.mk,
  Application.mk}`, `AndroidManifest.xml`, `build.gradle`, `gradle/`,
  `gradle.properties`, `local.properties`, `settings.gradle`,
  `android.debug.keystore`, `libs/`.
- `Projects/AndroidPrebuilt/`
- `Projects/` — the now-empty parent directory.
- `JKXR-PCVR-Installer/` — after extracting the weapon pk3s above. Contains the
  Windows Inno Setup `.iss` installers, `vr_splash.bmp`,
  `packaged_mods_credits.txt`.
- `JKXR/windows/` — Windows OpenXR glue.
- `JKXR/android/` — Android OpenXR glue + `argtable3.{c,h}`.
- `java/` — Android companion app (Java).
- `res/` — Android launcher resources (mipmap icons, values).
- `make_z_vr_assets_pk3.bat` — Windows batch (the `.sh` equivalent stays).
- All `*.mk` files inside the source tree (`Android.mk`, `Android_client_ja.mk`,
  `Android_client_jo.mk`, `Android_game_ja.mk`, `Android_game_jo.mk`,
  `Android_gles_ja.mk`, `Android_gles_jo.mk`, `Application.mk`).
- `code/android/` — Android platform glue (input/sound/window/main).
- `code/win32/` — Windows platform glue + resources.
- `code/macosx/` — macOS platform glue; not a target, no need to keep.

**Keep as dead reference (not compiled, removed from any build references):**

- `code/rd-gles/` — shader-based GLES renderer with VR stereo replay features
  that `rd-vanilla` lacks. High reference value for the Vulkan port.

**Keep and actively use:**

- `JKXR/linux/` — the Linux OpenXR glue (`JKXR_SurfaceView.cpp`,
  `TBXR_Common.{cpp,h}`).
- Everything else in `OpenJK/` that the Linux build compiles.

### Self-containment guard

Before deleting `Projects/Android/jni/{OpenXR-SDK, SupportLibs}`, verify the
Linux build depends only on files inside `OpenJK/` (the CMake tree references
`${OpenJKLibDir}/openxr/include` = `OpenJK/lib/openxr/include`, and bundled
libs under `OpenJK/lib/`). If anything in the Linux build references
`OpenXR-SDK` or `SupportLibs`, relocate that dependency into `OpenJK/lib/`
first. This is checked as the first implementation step, before any deletion.

### CMake simplification

In `OpenJK/CMakeLists.txt` and `OpenJK/code/CMakeLists.txt`, collapse the
platform conditionals to the Linux path:

- `code/CMakeLists.txt`: hardcode
  `set(JKXRPlatformDir "${CMAKE_SOURCE_DIR}/JKXR/linux")` (drop the `WIN32`
  branch). Drop the `win32` resource file block and the `sys_win32.cpp` /
  `con_passive.cpp` branch; keep the `else()` branch (`sys_unix.cpp` +
  `con_tty.cpp`).
- `CMakeLists.txt`: drop the `UseInternal*` WIN32 defaults block and the MSVC
  flag block (the GCC/Clang block is what the Linux build uses). Keep APPLE/BSD
  branches as-is to minimize diff and risk.

The `rd-gles` directory is not added by CMake today (it is only built via the
Android `.mk` files, which are being deleted), so no CMake change is needed to
exclude it.

### Path reference updates

All of the following are updated from `Projects/Android/jni/OpenJK` to `OpenJK`,
and weapon-pk3 paths from `JKXR-PCVR-Installer/...` to `assets/weapons/...`:

- `build_linux.sh`
- `install_linux.sh`
- `packaging/arch/PKGBUILD`
- `README.md`
- `BUILDING_LINUX.md`

### Build, package, tag & verification flow

1. **Relocate + delete** — `git mv` the source; relocate weapon pk3s; `git rm`
   the scaffolding; prune CMake conditionals; update path references.
2. **Build** — `./build_linux.sh`. Confirm the 6 binaries + 3 pk3s:
   - `OpenJK/build-linux/openjk_sp.x86_64`, `openjo_sp.x86_64`
   - `OpenJK/build-linux/code/rd-vanilla/{rdsp,rdjosp}-vanilla_x86_64.so`
   - `OpenJK/build-linux/code/game/jagamex86_64.so`,
     `codeJK2/game/jospgamex86_64.so`
   - `assets/z_vr_assets_{base,jka,jko}.pk3`
3. **Package** — clean stale makepkg artifacts
   (`packaging/arch/{src,pkg,JKXR,JKXRL}`), add them to `.gitignore`, then
   `makepkg -f` in `packaging/arch/`. Confirm `jkxrl-git-*.pkg.tar.zst` is
   produced and that `pacman -Qpl` on it lists the expected
   `/usr/lib/jkxr/*`, `/usr/share/jkxr/{jka,jko}/*`, `/usr/bin/jkxr-jk{a,o}`.
4. **Verify (user)** — the user launches both games via Steam in VR mode. The
   launchers already target `/games/SteamLibrary/steamapps/common/Jedi
   {Academy,Outcast}/` via `JKXR_J{KA,O}_GAMEDATA`. The verification bar for
   this (headless) environment is "builds + packages"; full VR gameplay is
   confirmed by the user.
5. **Commit + tag** — a single commit
   `Remove Windows/Android targets; Linux-only (v0.3)` then an annotated
   `git tag -a v0.3`. Done **only after** the package builds. Not pushed.

## Verification (acceptance criteria)

- [ ] `./build_linux.sh` succeeds and produces all 6 binaries + 3 pk3s.
- [ ] `makepkg` in `packaging/arch/` produces a valid
      `jkxrl-git-*.pkg.tar.zst` with the expected file list.
- [ ] No remaining references to `Projects/Android`, `JKXR-PCVR-Installer`,
      `JKXR/windows`, `JKXR/android`, `*.mk`, or `make_z_vr_assets_pk3.bat` in
      tracked files.
- [ ] `code/rd-gles/` is present but not compiled.
- [ ] User confirms both games launch and play in VR from the new package.
- [ ] Commit + annotated tag `v0.3` created (not pushed).

## Risks

- **Hidden dependency on `SupportLibs`/`OpenXR-SDK`**: mitigated by the
  self-containment guard (checked first, before deletion).
- **Path reference missed**: mitigated by grepping for the old path strings
  across tracked files after the edits.
- **Larger diff than expected**: the move is mechanical (`git mv` preserves
  history); CMake edits are localized to platform-selection blocks.
