# Remove Unreachable Windows References — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Remove all unreachable Windows-target code from the JKXRL engine — Windows-only files, `if(WIN32)`/`WIN64`/`MSVC` CMake blocks, the `UseInternal*` WIN32 defaults, and every `#ifdef _WIN32`/`_WIN32`/`_MSC_VER` preprocessor branch — since there will never be a Windows port.

**Architecture / directive:** User policy (recorded): "no future port for Windows — get rid of all unreachable Windows references." This is a mechanical, **Linux-binary-neutral** cleanup: every `#ifdef _WIN32` branch and Windows-only file is preprocessor-dead / not-compiled on Linux, so removing them cannot change the Linux binary. Verification is therefore **build-green + package-green**, not a gameplay re-test. The load-bearing file is `shared/qcommon/q_platform.h` (platform-detection cascade) — edit carefully.

**Spec:** User directive (this session) + the scope survey in `.superpowers/sdd/progress.md`. Branch: `windows-cleanup` (off SDL3-merged `main`).

## Global Constraints

- **Linux-binary-neutral** — the cleanup must not change the compiled Linux binary's behavior. Removing a Windows branch must keep its `#else` (Linux) counterpart intact; never delete both arms.
- **Canonical: keep Linux, drop Windows.** For `#ifdef _WIN32 ... #else [linux] ... #endif`, remove the `#if`/Windows-arm/`#else`, keep the Linux arm (de-indented). For Windows-only blocks with no `#else`, remove the whole block.
- **No behavior changes**; no new features. Pure dead-code removal.
- **Verification = `./build_linux.sh` green + `makepkg` green.** No user gameplay re-test (binary is provably unchanged). `codemp/` (MP, never built) cleanup is in-scope but lowest priority.
- x86_64. No pushing. Working dir: `/home/patola/workspace/opencode/JKXRL`.

## Inventory (from scope survey)

- **Windows-only files to delete:** `OpenJK/shared/sys/sys_win32.cpp`, `con_win32.cpp`, `con_passive.cpp`, `win_manifest.manifest`. (Referenced in `codemp/CMakeLists.txt` inside `if(WIN32)` blocks — remove those refs too.)
- **CMake:** `OpenJK/CMakeLists.txt` (lines ~93-141 `if(WIN32)` architecture detection + WIN64; ~58-72 `UseInternal*` WIN32/APPLE defaults + options; ~128 `if(WIN32...)` CMake-version warning; ~179 `if(WIN64)`; ~254 `if(WIN32)` libgcc-static; ~349+ `if(UseInternal*)` blocks — the UseInternal machinery is Windows-only-bundling, remove the WIN32-bundling paths). `code/CMakeLists.txt`, `code/rd-vanilla/CMakeLists.txt:166 if(WIN32)`, `code/game/CMakeLists.txt`, `codeJK2/game/CMakeLists.txt`, `codemp/*.cmake` `if(WIN32)` blocks.
- **`q_platform.h`:** the `#if defined(WIN64)...` / `#elif defined(_WIN32)...` top cascade (lines ~32-118) — collapse to the Linux/MACOS_X-neutral branch.
- **Preprocessor branches:** ~105 sites / 46 files. Heaviest: `codemp/` (CMakeLists, qcommon/files.cpp, common.cpp, net_ip.cpp, rd-dedicated/qgl.h, rd-vanilla/glext.h, icarus/Tokenizer.cpp, mp3code/config.h), `shared/` (q_platform.h, q_math.c, snapvector.cpp, sys_main.cpp, sdl_input.cpp, sdl_window.cpp), `code/` (client/snd_dma.cpp, qcommon/files.cpp+common.cpp+hstring.cpp, rd-vanilla/glext.h, mp3code/config.h), `codeJK2/` (icarus/Tokenizer.cpp, game/CMakeLists.txt).

---

### Task 1: Delete Windows-only files + clean all CMake `if(WIN32)`/`UseInternal*` blocks

**Files:** `OpenJK/shared/sys/{sys_win32,con_win32,con_passive}.cpp`, `win_manifest.manifest` (delete); `OpenJK/CMakeLists.txt`, `OpenJK/code/CMakeLists.txt`, `OpenJK/code/rd-vanilla/CMakeLists.txt`, `OpenJK/code/game/CMakeLists.txt`, `OpenJK/codeJK2/game/CMakeLists.txt`, `OpenJK/codemp/CMakeLists.txt` (edit).

- [ ] **Step 1:** `git rm OpenJK/shared/sys/sys_win32.cpp OpenJK/shared/sys/con_win32.cpp OpenJK/shared/sys/con_passive.cpp OpenJK/shared/sys/win_manifest.manifest`. (First confirm none are compiled on Linux: they're only in `codemp/CMakeLists.txt` `if(WIN32)` blocks + the already-collapsed `code/CMakeLists.txt` — both false on Linux.)
- [ ] **Step 2:** In every CMakeLists, remove the `if(WIN32)`/`if(WIN64)`/`if(MSVC)` blocks and their Windows-arm contents, keeping any `else()` (Linux) arm. In `OpenJK/CMakeLists.txt`: remove the WIN32/WIN64 architecture-detection block (keep the Linux `Architecture` path), the `UseInternal*Default` WIN32 block + the `option(UseInternal* ...)` entries (Windows-only bundling — Linux uses system libs), the MSVC compiler block (already removed in v0.3? verify), and any `if(WIN32)` libgcc-static / manifest blocks. In `codemp/CMakeLists.txt`: remove the `if(WIN32)` blocks referencing the deleted files (lines ~537-538, 666, 669) and the `if(WIN32)` resource/win32 blocks.
- [ ] **Step 3:** Verify no `if(WIN32)`/`WIN64`/`UseInternal`/`MSVC` remain in CMake: `rg -n 'if\s*\(\s*WIN32|if\s*\(\s*WIN64|if\s*\(\s*MSVC|UseInternal' OpenJK/**/CMakeLists.txt`. Expected: empty.
- [ ] **Step 4:** Build: `cd /home/patola/workspace/opencode/JKXRL && rm -rf OpenJK/build-linux && ./build_linux.sh`. Expected green. (CMake-only changes + file deletions; build should stay green.)
- [ ] **Step 5:** Commit: `Remove Windows-only files and CMake if(WIN32)/UseInternal blocks`. Selective add.

---

### Task 2: Collapse `q_platform.h` + `shared/` preprocessor branches

**Files:** `OpenJK/shared/qcommon/q_platform.h`, `q_math.c`, `shared/sys/snapvector.cpp`, `sys_main.cpp`, `sdl/sdl_input.cpp`, `sdl/sdl_window.cpp`.

- [ ] **Step 1: `q_platform.h` (load-bearing).** Collapse the platform-detection cascade: remove the `#if defined(WIN64)...` and `#elif defined(_WIN32)...` arms (lines ~32-118), keeping the Linux/`__linux__`/`__x86_64__` definitions (ARCH_STRING, etc.). Be meticulous — this header defines macros used everywhere. After editing, the Linux build must still produce the same `ARCH_STRING="x86_64"` etc.
- [ ] **Step 2:** For each other `shared/` file, remove `#ifdef _WIN32`/`_MSC_VER` Windows arms, keep the Linux/`#else` arm. (`rg -n '_WIN32|_MSC_VER|WIN32' OpenJK/shared/` to find them all.)
- [ ] **Step 3:** Build green (`./build_linux.sh`).
- [ ] **Step 4:** Commit: `Collapse q_platform.h + shared/ Windows preprocessor branches to Linux-only`.

---

### Task 3: `code/` + `codeJK2/` preprocessor branches

**Files:** `OpenJK/code/client/snd_dma.cpp`, `code/qcommon/{files,common,hstring}.cpp`, `code/rd-vanilla/glext.h`, `code/mp3code/config.h`, `codeJK2/icarus/Tokenizer.cpp`, and any other `code/`/`codeJK2/` files with `_WIN32`/`_MSC_VER`.

- [ ] **Step 1:** `rg -n '_WIN32|_MSC_VER|WIN32|#include\s*<windows\.h>' OpenJK/code/ OpenJK/codeJK2/ --glob '!**/glext.h'` — enumerate all sites. (Note: `glext.h` is a vendored GL extension header with WGL/WIN32 entries — for `rd-vanilla/glext.h`, removing its WGL `_WIN32` parts is optional since it's a 3rd-party-ish header; prefer leaving glext.h alone unless trivially safe, and note it.)
- [ ] **Step 2:** For each engine source file (not vendored headers), remove the Windows arms, keep Linux. Remove stray `#include <windows.h>` that sit inside the removed Windows blocks.
- [ ] **Step 3:** Build green.
- [ ] **Step 4:** Commit: `Remove Windows preprocessor branches in code/ and codeJK2/`.

---

### Task 4: `codemp/` branches + final verify + package

**Files:** `OpenJK/codemp/` files with `_WIN32` (qcommon/files.cpp, common.cpp, net_ip.cpp, rd-dedicated/qgl.h, rd-vanilla/glext.h, icarus/Tokenizer.cpp, mp3code/config.h, CMakeLists already done in T1).

- [ ] **Step 1:** `rg -n '_WIN32|_MSC_VER|WIN32' OpenJK/codemp/` — enumerate. Remove Windows arms, keep Linux, in each. (codemp is not built — these are doubly-dead — but the directive says remove all. Lower priority; if a file is purely Windows with no Linux arm, remove the whole block.) Leave vendored glext.h/headers alone unless trivial.
- [ ] **Step 2:** **Final no-Windows-refs check:** `rg -n '_WIN32|_MSC_VER|WIN32|#include\s*<windows\.h>' OpenJK/ --glob '!OpenJK/lib/**' --glob '!**/glext.h'` → expect empty (or only intentional vendored-header entries, documented).
- [ ] **Step 3:** Clean build green: `rm -rf OpenJK/build-linux && ./build_linux.sh`.
- [ ] **Step 4:** Package: `cd packaging/arch && cp PKGBUILD PKGBUILD.bak && sed -i 's#github.com/Patola/JKXRL.git#file:///home/patola/workspace/opencode/JKXRL#' PKGBUILD && makepkg -f && mv PKGBUILD.bak PKGBUILD`. Confirm package builds + contents.
- [ ] **Step 5:** Commit any final fixes. Note in ledger: Windows refs removed, build+package green, binary-neutral (no gameplay re-test).

---

## Acceptance Criteria

- [ ] `rg '_WIN32|_MSC_VER|WIN32|#include\s*<windows\.h>' OpenJK/` (excl. vendored `lib/` + `glext.h`) → empty.
- [ ] Windows-only files (`sys_win32.cpp`, `con_win32.cpp`, `con_passive.cpp`, `win_manifest.manifest`) deleted.
- [ ] No `if(WIN32)`/`WIN64`/`MSVC`/`UseInternal*` in any CMakeLists.
- [ ] `q_platform.h` collapsed to the Linux platform path; `ARCH_STRING` still `"x86_64"`.
- [ ] `./build_linux.sh` green from clean; `makepkg` green; package contents intact.
- [ ] **No user gameplay re-test** (binary-neutral by construction).

## Risks

- **`q_platform.h` is load-bearing** — a wrong collapse breaks every target. Mitigated by immediate build-green check in T2 + keeping the exact Linux macro values.
- **Accidentally removing the Linux `#else` arm** — caught by the build (Linux code would then reference undefined Windows symbols → compile error). Mitigated by per-task build-green.
- **Vendored headers (glext.h)** — leave alone unless trivially safe; they're 3rd-party GL extension headers.
