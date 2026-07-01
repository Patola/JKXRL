# Vulkan Renderer Port — Viability Assessment & M0 Design

**Status:** Approved design (pending M0 implementation plan)
**Date:** 2026-07-01
**Scope of this document:** (1) the viability assessment for porting JKXRL's
renderer to Vulkan; (2) the milestone decomposition for the full port; (3) the
detailed design of **M0 (Foundation/vertical-slice)**, the first sub-project.

## Context

JKXRL currently renders with **`rd-vanilla`**, a classic Quake-3-era
**fixed-function OpenGL** renderer (compatibility profile): immediate-mode
`glBegin/End`, the Q3 state machine (`GL_State`/`GL_TexEnv`), client-side
vertex arrays, **no VBOs, no GLSL**. The only programmable shaders are two
ARB-assembly programs used solely for the dynamic-glow effect. The source says
so itself: `code/rd-vanilla/tr_shadows.cpp:247` — *"needs a programmable
(GLSL) pipeline this fixed-function renderer does not have."*

The motivation for a Vulkan port: a more modern, programmable graphics
architecture for future engine work, and a native-Vulkan path for the
eventual ARM64 Steam Frame headset (deferred — this effort targets **x86_64
PC only**; ARM64 is a separate later effort). The stated priority is
**completeness over schedule** — a full, correct Vulkan port, no compromises
for time.

## Viability Assessment

**Verdict: feasible, but it is a full renderer rewrite — not a backend swap.**
Two facts make it tractable; one fact makes it large.

### What makes it tractable (the clean seams)

1. **The renderer is a loadable module behind a clean contract.** The engine
   loads the renderer `.so` via `dlopen` + `GetRefAPI` and communicates only
   through two function-pointer structs:
   - `refexport_t` (renderer→engine, ~150 pointers) —
     `code/rd-common/tr_public.h:143-389`. `REF_API_VERSION = 18`.
   - `refimport_t` (engine→renderer) — `tr_public.h:35-136`.
   - Engine loader: `code/client/cl_main.cpp:1095-1214` (`CL_InitRef`), default
     lib `rdsp-vanilla`/`rdjosp-vanilla`, selected via the `cl_renderer` cvar.
   - **A new `rd-vulkan.so` can plug in with no engine logic changes** — only
     the SDL window path and the OpenXR binding (below) need Vulkan arms.

2. **The OpenXR rebind and SDL window are small and mechanical** (~300 lines
   total, one file each):
   - `JKXR/linux/TBXR_Common.cpp` — replace `XrGraphicsBindingOpenGLXlibKHR`
     with `XrGraphicsBindingVulkanKHR`; `XR_KHR_opengl_enable` →
     `XR_KHR_vulkan_enable`. All required structs already ship in the bundled
     `lib/openxr/include/openxr_platform.h` (`XrGraphicsBindingVulkanKHR`,
     `XrGraphicsRequirementsVulkanKHR`, `XrSwapchainImageVulkanKHR`).
   - `shared/sdl/sdl_window.cpp` — add a `GRAPHICS_API_VULKAN` branch
     (`SDL_WINDOW_VULKAN`, `SDL_Vulkan_CreateSurface`).

### What makes it large (the work)

3. **Everything under `tr_*` must be rewritten.** `qgl.h` is just
   `#define qglFoo glFoo` (no indirection to repoint). There are ~980 real
   `qgl*` call sites and 247 immediate-mode sites in `code/rd-vanilla/`. Every
   fixed-function concept — multitexture `GL_TexEnv` combines, `GL_State`
   blend/alpha/depth filters, matrix stacks, `glAlphaFunc`, fog, display lists,
   the Q3 shader-script system — must be re-expressed as Vulkan pipelines +
   GLSL→SPIR-V. This is a from-scratch render-path rewrite measured in
   thousands of lines, which is why the work is decomposed into milestones.

### Correction on `rd-gles`

The v0.3 design doc characterized `rd-gles` as "shader-based." **That was
inaccurate.** `rd-gles` is *also* fixed-function (OpenGL ES 1.x,
`<GLES/gl.h>`, not GLES2). Its only genuine unique value is the **VR
stereo-replay capture/replay** feature (`code/rd-gles/tr_cmds.cpp:749-819`:
`RE_VR_BeginStereoReplayCapture` / `RE_VR_ReplayStereoFrame` /
`RE_VR_CancelStereoReplayCapture`), which `rd-vanilla` lacks and which is
ported forward in **M6**. `rd-gles` is **not** a shortcut to a Vulkan backend.

### Rejected alternative

Keeping `rd-vanilla` and presenting it through a GL-on-Vulkan translation
layer (Zink) is rejected: it forgoes the "modern architecture" goal, and the
OpenXR session would still need the native Vulkan binding path. The user has
explicitly chosen a complete native Vulkan renderer.

## Architecture Decisions (bind the whole port)

- **Vulkan stack:** Vulkan-Hpp (official C++ RAII bindings) +
  VulkanMemoryAllocator (VMA) for memory suballocation. Both are header-only
  (Vulkan-Hpp via the Vulkan SDK; VMA vendored into `OpenJK/lib/vma/`).
- **Module, not a fork:** a new clean `OpenJK/code/rd-vulkan/` module. No GL
  code carried over.
- **Renderer owns the Vulkan device; OpenXR vets and binds it.** The renderer
  creates `VkInstance`/`VkDevice` consulting OpenXR's required extensions;
  OpenXR binds them via `XrGraphicsBindingVulkanKHR`.
- **Presentation is OpenXR-driven**, matching the GL path (which skips
  `SDL_GL_SwapWindow`): render into `XrSwapchainImageVulkanKHR` → `VkImage` →
  `VkFramebuffer`; `xrEndFrame` presents. No desktop `VkSwapchainKHR` in M0.
- **Coexist with `rd-vanilla`** during M0–M6, selectable via `cl_renderer`
  (`rdsp-vulkan` / `rdjosp-vulkan`). `rd-vanilla` is deprecated only at M6.
- **GLSL→SPIR-V:** build-time offline compilation (CMake `FindVulkan`'s
  `Vulkan_GLSLC_EXECUTABLE` / `Vulkan_GLSLANG_VALIDATOR_EXECUTABLE`) for fixed
  shaders in M0. Runtime/load-time compilation (shaderc) is introduced in M1,
  when the dynamic Q3 shader-script system requires generated pipelines.

## Milestone Decomposition

The full port is too large for one spec/plan. Each milestone is a sub-project
with its own spec→plan→implement cycle.

| Milestone | Deliverable | Success criterion |
|---|---|---|
| **M0** | Foundation/vertical-slice: Vulkan device + OpenXR binding + SDL_Vulkan window + `rd-vulkan` module skeleton + clear-color, then a **fullscreen-textured quad** | A textured frame appears in the headset via OpenXR (`cl_renderer rd-vulkan`) |
| M1 | 2D/UI rendering (menus, text, console) + texture upload + the Q3 shader-system core (runtime GLSL generation introduced here) | In-game menus render correctly in VR |
| M2 | BSP world rendering: tr_bsp/tr_world/tr_surface, lightmaps, multitexture combines as GLSL | A map loads and renders |
| M3 | Mesh + Ghoul2 model rendering: tr_model, tr_mesh, ghoul2 | Characters and weapons render |
| M4 | Effects: tr_WorldEffects (weather), tr_sky, tr_surfacesprites, particles | Effects render |
| M5 | The five-level shadow system (blob, stencil, translucent stencil, soft/Ultra) | All shadow levels work |
| M6 | VR stereo-replay (port `rd-gles` capture/replay), optimization, polish, deprecate `rd-vanilla` | Feature parity; `rd-vanilla` removed |

---

## M0 Detailed Design (Foundation / vertical-slice)

**Goal:** prove the entire Vulkan → OpenXR → present pipeline end-to-end with
no gameplay rendering, via a new loadable `rd-vulkan` module.

### Components

1. **`OpenJK/code/rd-vulkan/`** — the new renderer module.
   - Exports `GetRefAPI` (`REF_API_VERSION 18`), returns a filled `refexport_t`.
   - Most `refexport_t` functions are M0 stubs (asset registration, scene,
     Ghoul2, fonts, etc. return defaults / no-op). The load-bearing ones for
     M0: `BeginRegistration`/`EndRegistration`, `BeginFrame`/`EndFrame`,
     `SubmitStereoFrame`, `SetColor`/`DrawStretchPic` (only enough for the test
     quad, optional), `Shutdown`, initialization.
   - Vulkan core: `VkInstance`, physical-device selection, `VkDevice`, queues,
     command pools/buffers, VMA allocator, render pass + framebuffers for the
     OpenXR swapchain images, a clear and a fullscreen-textured-quad draw.
   - Synthesizes the GL-shaped `glconfig_t` (`tr_types.h:233`) the engine/UI
     reads, so the engine is unaware the backend changed.

2. **Engine window path** (`shared/sdl/sdl_window.cpp`, `shared/sys/sys_public.h`):
   - Add `GRAPHICS_API_VULKAN` to `graphicsApi_t` and a Vulkan branch in
     `WIN_Init` (`SDL_CreateWindow(..., SDL_WINDOW_VULKAN)`,
     `SDL_Vulkan_CreateSurface`). `windowDesc_t` gains a Vulkan variant
     (carrying the `VkInstance` so SDL can make the surface).
   - `WIN_Present` gains a Vulkan arm (renders into OpenXR swapchain, calls
     `xrEndFrame`; no desktop swap, as today).

3. **OpenXR binding** (`JKXR/linux/TBXR_Common.cpp`):
   - Vulkan arm mirroring the GL path: define `XR_USE_GRAPHICS_API_VULKAN`,
     request `XR_KHR_vulkan_enable`, call `xrGetVulkanGraphicsRequirementsKHR`,
     `xrGetVulkanInstanceExtensionsKHR`, `xrGetVulkanDeviceExtensionsKHR`, fill
     `XrGraphicsBindingVulkanKHR` (instance, physicalDevice, device,
     queueFamilyIndex, queueIndex), and use `XrSwapchainImageVulkanKHR`
     (`VkImage`) for swapchain images. ~250 lines.

4. **Build system** (`OpenJK/code/rd-vulkan/CMakeLists.txt`, wired from
   `OpenJK/CMakeLists.txt` and `code/CMakeLists.txt` like `rd-vanilla`):
   - `find_package(Vulkan REQUIRED)` (provides `Vulkan::Headers`, `Vulkan::Volk`
     or the loader, `Vulkan_GLSLC_EXECUTABLE`).
   - Vendor VMA single-header at `OpenJK/lib/vma/include/vk_mem_alloc.hpp`.
   - Build-time GLSL→SPIR-V for M0's fixed shaders (a fullscreen-quad
     vertex+fragment pair), embedded as byte arrays.
   - Produces `rdsp-vulkan_x86_64.so` (JKA) and `rdjosp-vulkan_x86_64.so`
     (JK2), per-game, matching the rd-vanilla pattern; add `BuildSPRdVulkan` /
     `BuildJK2SPRdVulkan` toggles (default ON alongside the vanilla ones).

### Data flow (the crux)

```
renderer creates VkInstance
  (enable OpenXR's xrGetVulkanInstanceExtensionsKHR() exts + SDL's VK_KHR_surface)
   → SDL creates the Vulkan window + VkSurfaceKHR (using that instance)
   → renderer creates VkDevice
       (enable OpenXR's xrGetVulkanDeviceExtensionsKHR() exts)
   → OpenXR session created with XrGraphicsBindingVulkanKHR{instance,pdev,device,queueFamily,queue}
   → OpenXR swapchain images = XrSwapchainImageVulkanKHR (VkImage)
   → per frame: acquire/wait OpenXR swapchain image → render (clear / fullscreen-textured quad)
       into its VkFramebuffer → release → xrEndFrame (OpenXR presents)
```

### M0 deliverable (vertical slice)

Two checkpoints:
1. **Clear-color frame** in the headset — proves device → OpenXR binding →
   present pipeline.
2. **Fullscreen-textured quad** — proves vertex + fragment shaders, graphics
   pipeline, descriptor set, texture upload (`VkImage`), sampler, and VMA
   memory allocation. This is the minimal draw pipeline every later milestone
   depends on; a bare clear is too thin to de-risk the rewrite.

### Verification

This environment is headless (no VR/OpenXR-runtime/display). M0 acceptance for
this box: both `rd-vulkan` `.so` build + the Arch package build green. **You**
run the engine with `cl_renderer rd-vulkan` (or a launcher option) under your
WiVRn runtime and confirm a clear-color, then a textured, frame appears in the
headset.

### Out of scope for M0

- Any gameplay rendering: BSP world, models, UI menus, effects, shadows
  (M1–M5).
- VR stereo-replay capture/replay (M6).
- Desktop mirror window (M0 is OpenXR-presented only).
- ARM64 (x86_64 only).

### Risks

- **OpenXR runtime Vulkan support:** WiVRn must expose `XR_KHR_vulkan_enable`
  (it does). Confirmed requirement before each VR test.
- **VkInstance/device extension negotiation with OpenXR:** the cooperative
  creation (renderer creates the device, OpenXR dictates extensions) is the
  most intricate part of M0; mitigated by following the OpenXR Vulkan sample
  pattern and the existing GL binding structure.
- **Vulkan SDK availability on the build/Arch package:** `vulkan-devel` /
  `shaderc` become build dependencies; the PKGBUILD `makedepends` must add
  them, and `libvulkan` a runtime dependency.
