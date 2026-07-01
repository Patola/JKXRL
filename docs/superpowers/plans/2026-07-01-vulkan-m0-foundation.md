# Vulkan M0 — Foundation / Vertical-Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a new loadable `rd-vulkan` renderer module that drives a Vulkan device through OpenXR and renders a clear-color, then a fullscreen-textured-quad, frame into the headset — proving the full Vulkan→OpenXR→present pipeline end-to-end with no gameplay rendering.

**Architecture:** A new clean `OpenJK/code/rd-vulkan/` module implements the existing `refexport_t` / `GetRefAPI` (`REF_API_VERSION 18`) contract, so the engine loads it via `cl_renderer rdsp-vulkan|rdjosp-vulkan` with no engine-logic changes. The renderer owns the Vulkan device (Vulkan-Hpp + VMA); OpenXR vets and binds it (`XrGraphicsBindingVulkanKHR`); presentation is OpenXR-driven (renders into `XrSwapchainImageVulkanKHR` → `VkImage` → `VkFramebuffer`, `xrEndFrame` presents). `rd-vanilla` is untouched and remains the default.

**Tech Stack:** Vulkan 1.4 (Vulkan-Hpp C++ bindings), VulkanMemoryAllocator (VMA, vendored), OpenXR (`XR_KHR_vulkan_enable`), SDL2 (`SDL_WINDOW_VULKAN`), CMake (`find_package(Vulkan)`, build-time SPIR-V via `glslc`).

**Spec:** `docs/superpowers/specs/2026-07-01-vulkan-port-assessment-and-m0-design.md`

## How Vulkan code is specified in this plan

This plan rewrites a graphics subsystem in Vulkan. CMake, GLSL shaders, enums, the `GetRefAPI` stub pattern, and PKGBUILD edits are given verbatim. The Vulkan/OpenXR C++ is specified by **(a) the exact file responsibilities and interfaces, (b) the ordered API call sequence to implement, and (c) the existing OpenGL path to mirror (with file:line references)**. The implementer writes the Vulkan-Hpp C++ around those call sequences. Verification is **build-green** for coding tasks and **user-in-VR confirmation** for the two integration checkpoints (T4, T5) — there are no unit tests (a Vulkan+OpenXR renderer cannot be unit-tested in this headless environment).

## Global Constraints

- **x86_64 only.** No ARM64 code paths.
- **Vulkan-Hpp + VMA.** VMA is vendored at `OpenJK/lib/vma/include/` (single header, MIT); Vulkan-Hpp via the Vulkan SDK headers.
- **Coexist with `rd-vanilla`.** `rd-vanilla` stays the default `cl_renderer`, still built, behavior unchanged. `rd-vulkan` is opt-in via `cl_renderer rdsp-vulkan` (JKA) / `rdjosp-vulkan` (JK2).
- **No engine-logic changes** beyond: the `GRAPHICS_API_VULKAN` SDL window path, the OpenXR Vulkan binding arm in `TBXR_Common.cpp`, and the renderer-loader (already renderer-agnostic).
- **Release build flags unchanged** for all existing targets (`-O1 -fno-strict-aliasing`, `!lto`). Vulkan targets use the same.
- **No pushing.** Local commits on a feature branch only.
- **Vulkan SDK is present** (Vulkan 1.4.350, `glslc`, `glslangValidator`, `vulkan.h`, `libvulkan.so`); confirmed on the build machine.
- Working directory: `/home/patola/workspace/opencode/JKXRL`.

## File Structure (new + modified)

**New — the `rd-vulkan` module (`OpenJK/code/rd-vulkan/`):**
- `CMakeLists.txt` — build target, mirrors `code/rd-vanilla/CMakeLists.txt`.
- `tr_public_stub.h` / `tr_local.h` — renderer-local types (Vulkan handles, swapchain state).
- `tr_init.cpp` — `GetRefAPI` + stub `refexport_t` + Vulkan init/shutdown.
- `vk_device.cpp` — VkInstance/PhysicalDevice/Device/queues/VMA/command pool.
- `vk_swapchain.cpp` — OpenXR swapchain (VkImage) + framebuffer/render-pass management.
- `vk_render.cpp` — the frame loop (clear, then textured quad), pipelines, descriptors.
- `shaders/fullscreen.vert`, `shaders/fullscreen.frag` — the textured-quad GLSL (T5).
- `shaders/compile.cmake` — build-time `glslc` → embedded SPIR-V byte arrays.

**New — vendored VMA:** `OpenJK/lib/vma/include/vk_mem_alloc.h` (+ `LICENSE`).

**Modified — engine window/binding (shared, small):**
- `OpenJK/shared/sys/sys_public.h` — add `GRAPHICS_API_VULKAN`; Vulkan field on `windowDesc_t`.
- `OpenJK/shared/sdl/sdl_window.cpp` — `GRAPHICS_API_VULKAN` branch in `WIN_Init`/`WIN_Present`.
- `OpenJK/JKXR/linux/TBXR_Common.cpp` — OpenXR Vulkan binding arm (mirrors the GL arm).

**Modified — build wiring:**
- `OpenJK/CMakeLists.txt` — add `BuildSPRdVulkan`/`BuildJK2SPRdVulkan` options + `SPRDVulkanRenderer`/`JK2SPVulkanRenderer` names.
- `OpenJK/code/CMakeLists.txt` — `add_subdirectory("${SPDir}/rd-vulkan")`.
- `build_linux.sh`, `packaging/arch/PKGBUILD` — pass the new build flags + Vulkan deps.

---

### Task 1: Build scaffold — `rd-vulkan` module with all-stub `GetRefAPI`

**Files:**
- Create: `OpenJK/lib/vma/include/vk_mem_alloc.h`, `OpenJK/lib/vma/LICENSE`
- Create: `OpenJK/code/rd-vulkan/CMakeLists.txt`
- Create: `OpenJK/code/rd-vulkan/tr_init.cpp`, `OpenJK/code/rd-vulkan/tr_local.h`
- Modify: `OpenJK/CMakeLists.txt` (options + renderer names)
- Modify: `OpenJK/code/CMakeLists.txt` (add_subdirectory)

**Interfaces:**
- Produces: the `rd-vulkan` CMake targets `rdsp-vanilla_x86_64`→`rdsp-vulkan_x86_64` / `rdjosp-vulkan_x86_64` exporting `extern "C" Q_EXPORT refexport_t* QDECL GetRefAPI(int, refimport_t*)`. Later tasks implement the real Vulkan behind these stubs.

- [ ] **Step 1: Vendor VMA.** Download `vk_mem_alloc.h` (v3.x, MIT) into `OpenJK/lib/vma/include/` and its LICENSE into `OpenJK/lib/vma/LICENSE`. Verify: `ls OpenJK/lib/vma/include/vk_mem_alloc.h`.

- [ ] **Step 2: Add build options + renderer names** to `OpenJK/CMakeLists.txt`, mirroring the vanilla lines (`BuildSPRdVanilla` at ~line 44, `BuildJK2SPRdVanilla` at ~line 48, `SPRDVanillaRenderer` at ~line 139, `JK2SPVanillaRenderer` at ~line 148):
```cmake
option(BuildSPRdVulkan "Whether to create projects for the SP Vulkan renderer (rdsp-vulkan_x86_64.so)" ON)
option(BuildJK2SPRdVulkan "Whether to create projects for the jk2 sp Vulkan renderer (rdjosp-vulkan_x86_64.so)" ON)
```
and with the other binary names:
```cmake
set(SPRDVulkanRenderer "rdsp-vulkan_${Architecture}")
set(JK2SPVulkanRenderer "rdjosp-vulkan_${Architecture}")
```

- [ ] **Step 3: Add the subdirectory** to `OpenJK/code/CMakeLists.txt` right after line 30 (`add_subdirectory("${SPDir}/rd-vanilla")`):
```cmake
add_subdirectory("${SPDir}/rd-vulkan")
```

- [ ] **Step 4: Write `OpenJK/code/rd-vulkan/CMakeLists.txt`** mirroring `code/rd-vanilla/CMakeLists.txt` but minimal for M0 — just `tr_init.cpp` + the rd-common headers, linked against Vulkan + VMA. Key differences from the vanilla file:
```cmake
if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

if(BuildSPRdVulkan OR BuildJK2SPRdVulkan)
	find_package(Vulkan REQUIRED)   # provides Vulkan::Headers, Vulkan_GLSLC_EXECUTABLE

	set(SPRDVulkanFiles
		"${SPDir}/rd-vulkan/tr_init.cpp"
		"${SPDir}/rd-vulkan/tr_local.h"
		)
	set(SPRDVulkanRdCommonFiles
		"${SPDir}/rd-common/tr_public.h"
		"${SPDir}/rd-common/tr_types.h"
		"${SPDir}/rd-common/tr_common.h"
		)
	set(SPRDVulkanDefines ${SharedDefines} "RENDERER" "_JK2EXE")
	set(SPRDVulkanRendererIncludeDirectories
		${SharedDir} ${SPDir} "${SPDir}/rd-vulkan" "${SPDir}/rd-common"
		"${JKXRDir}" "${GSLIncludeDirectory}" "${OpenJKLibDir}"
		"${OpenJKLibDir}/vma/include" ${Vulkan_INCLUDE_DIRS})
	set(SPRDVulkanRendererLibraries ${Vulkan_LIBRARIES})

	function(add_sp_vulkan_project ProjectName Label)
		add_library(${ProjectName} SHARED ${SPRDVulkanFiles} ${SPRDVulkanRdCommonFiles})
		set_target_properties(${ProjectName} PROPERTIES PREFIX "")  # no lib prefix
		set_target_properties(${ProjectName} PROPERTIES COMPILE_DEFINITIONS "${SPRDVulkanDefines}")
		set_target_properties(${ProjectName} PROPERTIES INCLUDE_DIRECTORIES "${SPRDVulkanRendererIncludeDirectories}")
		set_property(TARGET ${ProjectName} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})
		target_link_libraries(${ProjectName} ${SPRDVulkanRendererLibraries})
	endfunction(add_sp_vulkan_project)

	if(BuildSPRdVulkan)
		add_sp_vulkan_project(${SPRDVulkanRenderer} "SP Vulkan Renderer")
	endif()
	if(BuildJK2SPRdVulkan)
		set(SPRDVulkanDefines ${SPRDVulkanDefines} "JK2_MODE")
		add_sp_vulkan_project(${JK2SPVulkanRenderer} "JK2 SP Vulkan Renderer")
	endif()
endif()
```

- [ ] **Step 5: Write the all-stub `tr_init.cpp`** exporting `GetRefAPI`. Every `refexport_t` field is a no-op/return-default stub except the ones M0 fills in later. Pattern mirrors `code/rd-vanilla/tr_init.cpp:1996-2054`:
```cpp
#include "tr_local.h"
#include "tr_public.h"

static refimport_t ri;

// M0 stubs — replaced with real Vulkan impl in later tasks.
static void RE_BeginRegistration(glconfig_t *config, intptr_t /*pVrClientInfo*/) { memset(config,0,sizeof(*config)); }
static void RE_EndRegistration(void) {}
static void RE_Shutdown(qboolean, qboolean) {}
static void RE_ClearScene(void) {}
static void RE_BeginFrame(stereoFrame_t) {}
static void RE_EndFrame(int*, int*) {}
static void RE_SubmitStereoFrame(void) {}
// ... one trivial stub per refexport_t function pointer in tr_public.h:143-389
//     (RegisterModel/RegisterSkin/RegisterShader return 0; qboolean returns qfalse; etc.)

extern "C" Q_EXPORT refexport_t* QDECL GetRefAPI(int apiVersion, refimport_t *refimp) {
	static refexport_t re;
	ri = *refimp;
	memset(&re, 0, sizeof(re));
	if (apiVersion != REF_API_VERSION) return NULL;
	#define REX(x) re.x = RE_##x
	REX(Shutdown); REX(BeginRegistration); REX(EndRegistration);
	REX(ClearScene); REX(BeginFrame); REX(EndFrame); REX(SubmitStereoFrame);
	// ... wire every other refexport_t field to its stub
	#undef REX
	return &re;
}
```
Enumerate the full `refexport_t` field list from `tr_public.h:143-389` and give each a stub. The fields M0 later makes real are the only ones that matter functionally; the rest stay stubbed through M0.

- [ ] **Step 6: Build the new targets.** Run `./build_linux.sh`. Expected: configures (finds Vulkan), builds `OpenJK/build-linux/code/rd-vulkan/rdsp-vulkan_x86_64.so` and `rdjosp-vulkan_x86_64.so`. The rest of the build is unchanged. If Vulkan isn't found, install `vulkan-devel` / ensure `Vulkan_INCLUDE_DIRS` resolves.

- [ ] **Step 7: Commit.** `git add` the new files + the 3 modified CMake files (selective — never `git add -A`). Message: `rd-vulkan: scaffold module + VMA + all-stub GetRefAPI (M0 T1)`.

---

### Task 2: SDL Vulkan window path (engine-side)

**Files:**
- Modify: `OpenJK/shared/sys/sys_public.h:162-199` (enum + `windowDesc_t`)
- Modify: `OpenJK/shared/sdl/sdl_window.cpp` (`WIN_Init` ~line 794, `WIN_Present` ~line 184)

**Interfaces:**
- Produces: a `GRAPHICS_API_VULKAN` path in `WIN_Init` that creates an `SDL_WINDOW_VULKAN` window. The renderer (T3) supplies the `VkInstance` via `windowDesc_t`; SDL creates the `VkSurfaceKHR` with `SDL_Vulkan_CreateSurface`. `WIN_Present`'s Vulkan arm calls `TBXR_submitFrame()` (no desktop swap, as the GL arm does).

- [ ] **Step 1: Add the enum value** in `sys_public.h` after `GRAPHICS_API_OPENGL` (line 165):
```c
	GRAPHICS_API_VULKAN,
```

- [ ] **Step 2: Add a Vulkan carrier to `windowDesc_t`** (`sys_public.h:187-199`). Add a `vk` sub-struct alongside the existing `gl` one (only used when `api == GRAPHICS_API_VULKAN`):
```c
	struct { void *instance; /* VkInstance, opaque to the engine */ } vk;
```

- [ ] **Step 3: Add the `WIN_Init` Vulkan branch** in `sdl_window.cpp`. In the window-creation area (where `SDL_GL_SetAttribute`/`SDL_CreateWindow` are, ~lines 555-647), branch on `desc->api`: for `GRAPHICS_API_VULKAN`, set the window flag to `SDL_WINDOW_VULKAN` (skip all `SDL_GL_*` calls and `SDL_GL_CreateContext`), and after `SDL_CreateWindow` create the surface:
```c
if (desc->api == GRAPHICS_API_VULKAN) {
	// windowFlags |= SDL_WINDOW_VULKAN;  (set where flags are assembled)
	VkSurfaceKHR surface;
	SDL_Vulkan_CreateSurface(screen, (VkInstance)desc->vk.instance, &surface);
	// store surface / pass back to the renderer via the returned window_t
}
```
Keep the `GRAPHICS_API_OPENGL` path exactly as-is. Surface ownership/handoff: the renderer owns the surface; the engine just creates it. Decide the handoff field on `window_t` (`sys_public.h`) and document it.

- [ ] **Step 4: Add the `WIN_Present` Vulkan arm** (~line 184, where `if (api == GRAPHICS_API_OPENGL)` currently routes to `TBXR_submitFrame()`): for `GRAPHICS_API_VULKAN`, route identically to `TBXR_submitFrame()` (OpenXR presents; no `SDL_GL_SwapWindow`).

- [ ] **Step 5: Build.** `./build_linux.sh`. Expected: the engine still builds and runs identically with the default GL renderer (the Vulkan branch is added but not exercised until T3–T4). The `rd-vulkan` stub module still builds.

- [ ] **Step 6: Commit.** Message: `engine: SDL Vulkan window path (GRAPHICS_API_VULKAN) (M0 T2)`.

---

### Task 3: Vulkan device + OpenXR Vulkan session

This task establishes the Vulkan device and the OpenXR session bound to it. The renderer creates the device; OpenXR vets and binds it. (No rendering yet — that's T4.)

**Files:**
- Create: `OpenJK/code/rd-vulkan/vk_device.cpp`, `vk_device.h`
- Modify: `OpenJK/code/rd-vulkan/tr_init.cpp` (call device init from `RE_BeginRegistration`)
- Modify: `OpenJK/JKXR/linux/TBXR_Common.cpp` (add the Vulkan binding arm)

**Interfaces:**
- Consumes: the OpenXR instance + the JKXR VR hooks from `refimport_t` (`TBXR_GetVRProjection`, `TBXR_GetFovTangentsForEye`, `TBXR_GetEyeStereoSeparation`, `TBXR_useScreenLayer` — all API-neutral, reused as-is).
- Produces: a live `VkInstance`/`VkPhysicalDevice`/`VkDevice`/queue + a created OpenXR session. T4 renders into the session's swapchain.

**Mirror the existing GL path.** The OpenGL binding lives entirely in `JKXR/linux/TBXR_Common.cpp`; the implementer reads it and writes the Vulkan equivalent:
- GL arm (~`TBXR_Common.cpp:41-42, 803-854, 970-979, 165-209`): `XR_KHR_opengl_enable`, grabs the GLX context from SDL, fills `XrGraphicsBindingOpenGLXlibKHR`, calls `xrGetOpenGLGraphicsRequirementsKHR`, swapchain images are `XrSwapchainImageOpenGLKHR` (GLuint) wrapped in FBOs.

- [ ] **Step 1: Vulkan binding arm in `TBXR_Common.cpp`.** Define `XR_USE_GRAPHICS_API_VULKAN` (alongside where the GL binding's platform define is set — note `XR_USE_GRAPHICS_API_OPENGL`/`XR_USE_PLATFORM_XLIB` are isolated to this TU). Request extension `XR_KHR_vulkan_enable`. Implement the Vulkan analogues, in the same order as the GL arm:
  1. `xrGetVulkanGraphicsRequirementsKHR(...)` → `XrGraphicsRequirementsVulkanKHR` (min Vulkan version + `VkPhysicalDeviceType`).
  2. `xrGetVulkanInstanceExtensionsKHR(...)` → the instance-extension list the renderer must enable.
  3. `xrGetVulkanDeviceExtensionsKHR(...)` → the device-extension list.
  4. Fill `XrGraphicsBindingVulkanKHR{ type, next, instance, physicalDevice, device, queueFamilyIndex, queueIndex }` and pass it as `sessionCreateInfo.next` to `xrCreateSession` (mirror the GL session creation at ~`:859`).
  5. Swapchain enumeration returns `XrSwapchainImageVulkanKHR` (a `VkImage`) per image — mirror the GL swapchain loop at ~`:165-209, 259-280`, but wrap each `VkImage` in a `VkFramebuffer` (T4 creates the render pass + framebuffers; here just store the `VkImage` list).

- [ ] **Step 2: Vulkan device core in `vk_device.cpp`** (Vulkan-Hpp + VMA). Implement, in this order:
  1. Create `vk::Instance` — enable `VK_KHR_surface` + the SDL surface extension (`SDL_Vulkan_GetInstanceExtensions`) + the OpenXR instance extensions from step 1.2 above.
  2. `SDL_Vulkan_CreateSurface` (the window/surface comes from the engine T2 path using this instance).
  3. Pick `vk::PhysicalDevice` (satisfying OpenXR's requirements from step 1.1; prefer discrete GPU).
  4. Create `vk::Device` enabling the OpenXR device extensions (step 1.3) + swapchain/graphics-queue family; find the graphics queue index OpenXR expects (passed to the binding struct).
  5. `vmaCreateAllocator` (VMA) wrapping the instance/physicaldevice/device.
  6. Create a `vk::CommandPool` for the graphics queue.

- [ ] **Step 3: Wire init.** `RE_BeginRegistration` (currently a T1 stub) calls into `vk_device` init, which cooperates with the OpenXR Vulkan binding to create the session. Hand the `VkInstance` to the engine's `WIN_Init` (T2 `windowDesc_t.vk.instance`) so SDL can make the surface.

- [ ] **Step 4: Build.** `./build_linux.sh`. Expected: `rd-vulkan` compiles with the real Vulkan device code + OpenXR Vulkan binding. (Does not yet render — T4 does.) Resolve any Vulkan-Hpp include/path issues (Vulkan-Hpp is in the SDK `<vulkan/vulkan.hpp>`).

- [ ] **Step 5: Commit.** Message: `rd-vulkan: Vulkan device + OpenXR Vulkan session (M0 T3)`.

---

### Task 4: Clear-color render loop through OpenXR  ← checkpoint 1 (user-in-VR)

**Files:**
- Create: `OpenJK/code/rd-vulkan/vk_swapchain.cpp`, `vk_swapchain.h`, `vk_render.cpp`, `vk_render.h`
- Modify: `OpenJK/code/rd-vulkan/tr_init.cpp` (`RE_BeginFrame`/`RE_EndFrame`/`RE_SubmitStereoFrame`)

**Interfaces:**
- Consumes: the session + `VkImage` swapchain list from T3.
- Produces: a per-frame clear-color image presented via OpenXR.

- [ ] **Step 1: Render pass + per-swapchain-image framebuffers** (`vk_swapchain.cpp`). Create a `vk::RenderPass` (one color attachment, the swapchain format) and, for each OpenXR swapchain `VkImage`, a `vk::Framebuffer` + a `vk::CommandBuffer` (from the T3 pool). Mirror the GL path's per-eye-FBO setup conceptually.

- [ ] **Step 2: Frame loop** (`vk_render.cpp` + `RE_BeginFrame`/`RE_EndFrame`/`RE_SubmitStereoFrame`). Per frame, per eye, mirror the GL acquire/present sequence (`TBXR_Common.cpp:259-280` `xrAcquireSwapchainImage`/`xrWaitSwapchainImage`/`xrReleaseSwapchainImage`, `:1321` `xrEndFrame`), but the work recorded into the image is: `begin command buffer` → `begin render pass` (load-op clear, a distinct clear color so it's obviously working) → `end render pass` → `end command buffer` → `submit` → `queue wait idle`. Projection/FOV/eye-separation come from the existing `TBXR_GetVRProjection`/`TBXR_GetFovTangentsForEye`/`TBXR_GetEyeStereoSeparation` hooks (API-neutral). Use `XrCompositionLayerProjectionView` per eye as the GL path does.

- [ ] **Step 3: Build + package.** `./build_linux.sh`; build the package (`packaging/arch`: `makepkg -f` with the local-source trick — see v0.3 plan T6). Both green.

- [ ] **Step 4: USER VERIFICATION (checkpoint 1).** This is headless; you run the engine with `cl_renderer rdsp-vulkan` (or `set cl_renderer rdsp-vulkan` in the engine console / a launch cvar) under WiVRn and confirm a **solid clear-color frame appears in the headset** for both eyes. If the OpenXR runtime errors, check it exposes `XR_KHR_vulkan_enable` (WiVRn does). Do not proceed to T5 until you see the clear frame.

- [ ] **Step 5: Commit.** Message: `rd-vulkan: clear-color render loop through OpenXR (M0 T4)`.

---

### Task 5: Fullscreen-textured-quad  ← checkpoint 2 (M0 done; user-in-VR)

Replaces the bare clear with a textured quad — proving shaders, pipeline, descriptor set, texture upload, sampler, VMA memory.

**Files:**
- Create: `OpenJK/code/rd-vulkan/shaders/fullscreen.vert`, `fullscreen.frag`
- Create: `OpenJK/code/rd-vulkan/shaders/compile.cmake`
- Modify: `OpenJK/code/rd-vulkan/CMakeLists.txt` (invoke the shader compiler + include generated SPIR-V)
- Modify: `OpenJK/code/rd-vulkan/vk_render.cpp` (pipeline + descriptor + draw)

- [ ] **Step 1: Write the GLSL shaders.**

`shaders/fullscreen.vert` (fullscreen triangle, no vertex buffer):
```glsl
#version 450
out vec2 uv;
void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    uv = pos;
}
```

`shaders/fullscreen.frag`:
```glsl
#version 450
layout(set = 0, binding = 0) uniform sampler2D tex;
in vec2 uv;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(tex, uv); }
```

- [ ] **Step 2: Build-time SPIR-V.** `shaders/compile.cmake` — compile each shader to a `.spv` and generate a C byte-array header (`glslc -mfmt=num` or a small wrapper) embedded into the binary. Use `Vulkan_GLSLC_EXECUTABLE` (set by `find_package(Vulkan)`). Wire it into `CMakeLists.txt` so the SPIR-V arrays are built before `vk_render.cpp`.

- [ ] **Step 3: Texture upload** (`vk_render.cpp`). Generate a small CPU test texture (e.g. a recognizable checker/gradient), create a `vk::Image` (+ `vk::ImageView` + sampler) via VMA (`VMA_MEMORY_USAGE_GPU_ONLY`), stage-upload it with a command buffer (host-visible staging buffer → `vkCmdCopyBufferToImage` with a transition barrier). This exercises the full upload path every later milestone needs.

- [ ] **Step 4: Graphics pipeline + descriptor set.** A `vk::DescriptorSetLayout` (one combined-image-sampler), `vk::PipelineLayout`, and a `vk::GraphicsPipeline` built from the two SPIR-V modules + the T4 render pass (no vertex input, fullscreen-triangle `gl_VertexIndex`). Allocate a descriptor set pointing at the test texture/sampler.

- [ ] **Step 5: Draw.** In the T4 frame loop, after `begin render pass`, bind the pipeline + descriptor set and `vkCmdDraw(3, 1, 0, 0)` (fullscreen triangle) before `end render pass`.

- [ ] **Step 6: Build + package.** `./build_linux.sh` + `makepkg`. Both green.

- [ ] **Step 7: USER VERIFICATION (checkpoint 2 — M0 complete).** You run `cl_renderer rdsp-vulkan` (and `rdjosp-vulkan` for JKO) under WiVRn and confirm the **test texture fills the headset view** for both eyes. This proves the full minimal Vulkan draw pipeline.

- [ ] **Step 8: Commit.** Message: `rd-vulkan: fullscreen-textured-quad (shaders, pipeline, texture upload) (M0 T5)`.

---

### Task 6: Package the Vulkan module + renderer selection

**Files:**
- Modify: `packaging/arch/PKGBUILD`
- Modify: `build_linux.sh`
- Modify: `packaging/arch/jkxr-jka`, `packaging/arch/jkxr-jko` (optional launcher option)

- [ ] **Step 1: Pass the Vulkan build flags** in `build_linux.sh` and `packaging/arch/PKGBUILD`'s `cmake` invocation, alongside the vanilla flags:
```
-DBuildSPRdVulkan=ON -DBuildJK2SPRdVulkan=ON
```

- [ ] **Step 2: PKGBUILD deps.** Add to `makedepends`: `vulkan-devel` (provides headers + `glslc`) and `shaderc` (if `glslc` is packaged separately from `vulkan-devel` on Arch — verify which provides `/usr/bin/glslc`; on this machine `glslc` reports 2026.2). Add to `depends`: `libvulkan`. Verify with `pacman -Qi`/`pacman -F glslc` which package owns `glslc`.

- [ ] **Step 3: Package the `.so`.** In the PKGBUILD `package()`, install the two new modules next to the others:
```
build/code/rd-vulkan/rdsp-vulkan_x86_64.so
build/code/rd-vulkan/rdjosp-vulkan_x86_64.so
```
into `/usr/lib/jkxr/`.

- [ ] **Step 4: Renderer selection.** `rd-vanilla` stays the default. Add an opt-in way to select Vulkan: the user runs `set cl_renderer rdsp-vulkan` (JKA) / `rdjosp-vulkan` (JKO) in the engine, **or** (optional) add a `JKXR_RENDERER` env var honored by the `jkxr-jka`/`jkxr-jko` launchers that prepends `+set cl_renderer rdsp-vulkan` to the engine args. Keep it opt-in — Vulkan is not the default until later milestones reach parity.

- [ ] **Step 5: Build the package** with the local-source trick (`makepkg -f` with `source` temporarily set to `git+file://$PWD`). Verify `bsdtar -tf` lists `usr/lib/jkxr/rdsp-vulkan_x86_64.so` and `rdjosp-vulkan_x86_64.so`. Restore the real source URL.

- [ ] **Step 6: Commit.** Message: `packaging: build + ship rd-vulkan, Vulkan deps, opt-in renderer selection (M0 T6)`.

---

## Acceptance Criteria (M0)

- [ ] `OpenJK/code/rd-vulkan/` exists; `rdsp-vulkan_x86_64.so` + `rdjosp-vulkan_x86_64.so` build and are shipped in the package.
- [ ] `rd-vanilla` still builds and remains the default `cl_renderer`; the GL path is behaviorally unchanged.
- [ ] Engine `GRAPHICS_API_VULKAN` SDL window path + `TBXR_Common.cpp` OpenXR Vulkan binding present and compile.
- [ ] **User confirms (T4):** `cl_renderer rdsp-vulkan` shows a clear-color frame in the headset via WiVRn.
- [ ] **User confirms (T5):** the test texture fills the headset view (both eyes), for both JKA and JKO.
- [ ] VMA vendored under `OpenJK/lib/vma/`; build-time SPIR-V via `glslc` works.
- [ ] PKGBUILD `makedepends` includes Vulkan/shaderc; `depends` includes `libvulkan`.

## Risks & mitigations

- **OpenXR/Vulkan cooperative device creation is the hardest part** (renderer creates the device, OpenXR dictates extensions, instance/device ext negotiation). Mitigation: follow the OpenXR `openxr_programmers_guide` Vulkan sample sequence and mirror the existing GL binding structure in `TBXR_Common.cpp`.
- **Vulkan-Hpp / VMA include & linking** in this older CMake setup. Mitigation: T1 validates the toolchain early with the stub module before any real Vulkan code.
- **No headless verification** of the integration. Mitigation: T4 and T5 are explicit user-in-VR gates; do not advance until the user confirms each.
- **WiVRn Vulkan support.** Confirmed it exposes `XR_KHR_vulkan_enable`; re-check at T4.
