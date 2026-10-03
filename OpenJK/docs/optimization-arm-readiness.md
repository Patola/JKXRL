# ARM-aware optimization plan

2026-10-03. Incorporates Patola's target notes after vegetation fog passed.
This is an engineering review and measurement plan, not a hardware benchmark
or a change to the installed renderer. Native ARM64 remains the eventual
target, without FEX in the engine/dependency execution path. Broad x86-64
optimization and the PC release still precede the hardware-specific port.

## Verified direction and qualifications

Valve documents native ARM64 among Steam Frame's supported execution models.
Use its current development instructions when hardware becomes available;
do not copy predicted arrival dates, module names, root-unlock commands,
partition layouts or a guessed sysroot from the supplied side-chat text.
[Valve platform documentation](https://partner.steamgames.com/doc/steamhardware/steamframe).

Use the Snapdragon/Adreno device named by the user as a planning target,
not as a substitute for enumerating the actual GPU, driver, memory heaps,
extensions, formats and limits. Native compilation removes CPU instruction
translation; it does not remove runtime, driver, synchronization or thermal costs.

### Geometry and memory

`VK_CreateSkinnedVertexStream` already allocates a 64 MiB persistently mapped,
host-visible/coherent vertex stream and (when compute skinning is enabled) a
16 MiB bone stream. Vegetation and dynamic model paths suballocate offsets.
This is not yet an independently retired multi-frame ring. Measure high-water
usage and lifetime before multiplying these allocations by frames in flight.

`vkCmdBindVertexBuffers` changes binding state; it is not a vertex upload or
an intrinsic GPU stall. Binding once and using firstVertex/baseVertex can
reduce command recording for compatible draws, but cannot eliminate required
material/depth/transparency ordering. Reuse immutable geometry and shared
stereo results before chasing bind-count targets.
[Vulkan binding contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindVertexBuffers.html).

Choose memory types from reported properties and workload. UMA can expose
host-visible/device-local memory and avoid redundant dynamic-buffer staging.
Host coherence concerns flush/invalidate operations, not safe reuse while a
GPU is still reading. Do not universally require every preferred flag or
remove optimal-image uploads. Keep correct noncoherent flush/alignment support
where needed and preserve the discrete-GPU path.
[Khronos memory guidance](https://docs.vulkan.org/guide/latest/memory_allocation.html),
[memory flags](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryPropertyFlagBits.html).

### Synchronization and pipeline compilation

The stereo submission path submits both eye command buffers together, then calls
`vkQueueWaitIdle` for every stereo frame. This is a concrete serialization
candidate, not proof of the current dominant bottleneck. It presently protects
shared command pools, mapped streams, descriptors, queries, transient geometry
and captures. Never simply delete the wait. Any replacement needs bounded
frame/resource retirement, OpenXR swapchain release ordering and transition,
save-preview, cinematic and teardown regression tests. More queued frames can
increase VR latency even when throughput improves.

Graphics/compute creation currently passes VK_NULL_HANDLE as the pipeline
cache. Add application-managed persistent caching as a separately measured
candidate. Cache in the user's cache directory, validate device/driver/cache
identity and blob size, use atomic writes and tolerate corrupt/incompatible
files by recreating an empty cache. Driver caches may already help; measure
cold versus warm startup/loading, not just steady FPS.
[Khronos pipeline caching](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html).

### Shadows and tile bandwidth

Current maps default to 2048, permit 256..4096 and allocate four direction
layers. Profile active groups, caster work, receiver masks, filtering and
attachment traffic separately. A 1024/2048 quality tier is a candidate, not
a universal hardware limit or an immediate default downgrade.

Ordinary subpass/input-attachment reads are framebuffer-local. A light-space
shadow map requires unrelated sample coordinates; spatial blur requires
neighbor samples. Therefore putting these into ordinary subpasses does not
make their traffic entirely tile-local. Evaluate load/store operations,
transient attachments, pass merging and attachment lifetimes only where the
actual dependency permits it. Optional newer tile features need capability
checks and a fallback, not a new unverified device requirement.
[Khronos tile-rendering guidance](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html).

### OpenXR, SDL3 and UI

We already create LOCAL/VIEW spaces and attempt STAGE, obtain per-eye sizes
from OpenXR recommendations and use OpenXR controller actions directly.
Desktop window bounds or panel pixel counts are not authoritative eye-buffer
sizes or IPD. Keep SDL desktop events separate from tracked poses/actions.
[OpenXR view configuration](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrViewConfigurationView.html).

The current world-locked console is accepted. A separate compositor-layer UI
is an optional later clarity experiment, not an ARM port requirement. Preserve
pose, stereo world rendering, ray hit testing and occlusion policy; do not
reintroduce the old whole-scene mono-panel regression. Some screen states
already use OpenXR quad composition, but that is not the spatial console path.

Do not force a desktop backend globally. Test available Wayland/X11 paths
when appropriate; OpenXR owns headset presentation. SDL3's documented hint
name is SDL_VIDEO_DRIVER, not the older SDL_VIDEODRIVER spelling in the notes.
[SDL3 video-driver hint](https://wiki.libsdl.org/SDL3/SDL_HINT_VIDEO_DRIVER).

### Toolchain and packaging

An architecture triple alone is insufficient: pin a compatible target sysroot,
libc baseline, OpenXR loader and all ARM64 engine/module dependencies. Keep
host shader/build tools separate from target libraries. Use portable ARM64
defaults first; target-specific instruction selection needs verified CPU/OS
features. Do not use the x86 host's -march=native for cross compilation.
[GCC AArch64 options](https://gcc.gnu.org/onlinedocs/gcc/AArch64-Options.html).

OpenJK/CMakeLists.txt currently forces Release -O1 and -fno-strict-aliasing
because of documented legacy crashes/miscompilations. Do not blindly replace
those with -O3/fast-math. Audit actual per-target flags, sanitize suspect code,
then compare optimization levels and vectorization in bounded hot paths.
Consider LTO/PGO only with representative profiles and correctness tests.

Package in a user-writable, relocatable location; keep saves/config/cache out
of the application tree. Audit relative RUNPATH and transitive dependencies,
but discover the platform's native GPU driver/runtime rather than bundling a
desktop driver. Cross-build/deployment details wait for a verified ABI/runtime;
root modification is not a requirement of the plan.

## Measurement sequence

1. Review/freeze the accepted checkpoint and deployment manifest before runtime
   changes. No new commit or tag is claimed by this document.
2. Capture an unchanged baseline for each game separately, with stable saves,
   view positions, scene labels and repeated cold/warm runs. Separate loading,
   steady viewing, combat/explosions and cinematics.
3. Measure CPU game/render time, command recording, GPU pass times, submission
   waits, OpenXR pacing, upload bytes, stream high-water marks, draw/state counts
   and p50/p95/p99/hitch counts. CPU/GPU work can overlap; do not sum these times
   naively or interpret intentional xrWaitFrame pacing as wasted work.
4. Rank measured bottlenecks. Investigate frame retirement/synchronization,
   redundant CPU geometry work and upload traffic; separately implement/test
   pipeline caching for compilation costs. One bounded change per comparison.
5. Profile shadow/lighting/vegetation/explosion overdraw and bandwidth next
   where data warrants it. Preserve accepted effects, alpha/depth ordering,
   stereo and controls; do not count disabling features as equivalent quality.
6. Repeat on native ARM64 hardware after the port: sustained thermal runs,
   real driver/memory behavior, target refresh and end-to-end VR latency.

Keep the user's WiVRn setup fixed for the initial desktop baseline: Envision
runtime, previously reported 150% / 3096x3243 per eye, 50% foveation, 140 Mbit/s,
90 Hz, normal sharpening/supersampling and no spacewarp. Reconfirm actual
settings before capture. WiVRn streaming costs are separate from engine GPU
time. Do not require thumbstick overlay toggles that skip cinematics or a
Perfetto SDK installation to begin. Use existing engine timing/capture tools.

90 Hz provides an 11.11 ms display interval, not the entire application's
available GPU budget. Compositor/runtime work and safety margin matter.
Lower-resolution and CPU-limited desktop sweeps can expose sensitivity, but
neither throttling an x86 PC nor scaling RX 7900 XTX timings predicts Adreno
performance. Track work/bytes as cross-platform indicators, not ARM FPS claims.

Benchmarks: JKA crowded ship, t1_rail directions, t2_wedge and missile-heavy
combat; JKO Mon Mothma, ns_streets, Artus red-pulse cargo room and the separate
`artus_mine` viewpoint `setviewpos -2560 -100 665 85`. Retain Yavin vegetation,
water, shadows, scope, console, cinematic and save-preview regression checks.
High white subtitle placement and four-second camera-help timeout remain
separate requested cosmetic follow-ups, not silently discarded.
