# Wedge performance baseline

Status (2026-09-08): console and cutout-illumination fixes accepted. The first
headset baseline is captured and analyzed below. A CPU-local MD3 upload
optimization is ready for the same-route comparison; keep visual settings fixed.

## Setup

Use Jedi Academy only, through the normal terminal launcher and the existing
Envision WiVRn server. No packaged-server switch, profiling rebuild, Perfetto,
or double-thumbstick statistics shortcut is needed. Start WiVRn/connect the
Quest before launching the capture. Keep the desktop game window focused and
not minimized, then put on the headset.

Keep the previously reported WiVRn settings: render scale 150% (3096x3243 per
eye), foveation 50%, bitrate 140 Mbit/s, normal supersampling/sharpening, 90 Hz,
no spacewarp. Report any changes instead of silently comparing different
settings. The collector does not verify these headset/server settings. Avoid
video recording or starting unrelated GPU workloads during the measurement.

The launcher enables `r_vulkanTiming 1`, selects color profile 0 and bloom 0,
and retains current shadows, texture quality, dynamic lighting, compute
skinning and LOD. It queries key cvars at startup and snapshots archived
renderer settings for interpretation. Do not toggle those settings mid-run.
Normal engine config persistence still applies to archived launch cvars;
the collector does not itself edit user configs or installed files.

Run:

```bash
python3 /home/patola/workspace/codex/JKXRL-active/tools/capture_vr_performance.py
```

This replaces the usual launch-and-tee command for this run. It still invokes
`jkxr-jka` with the installed Academy GameData path, preserving the environment
used to select the OpenXR runtime. It prints a unique output directory under
`/tmp/jkxrl-perf/`. `--note "..."` can record changed headset settings.

## Route

Use the same `t2_wedge` saves/area where low performance was observed. No cheats
or teleport commands are necessary. Use a quiet spot for the stationary
samples; leave enemies active only for the combat sample. Keep the saber on
but resting in approximately the same position during stationary samples.

1. Outdoors: choose a recognizable walkway view with several buildings in
   view, preferably where performance feels poor. Open the VR console, enter
   `echo PERF_OUTDOOR`, close it, and hold that position/direction for about
   45 seconds. Small natural head motion is fine; do not turn to another view.
2. Hologram room: face the hologram from the comparison-video position. Enter
   `echo PERF_HOLOGRAM`, close the console, then remain there for 45 seconds.
3. Reverse view: at the same position, face the opposite wall, putting the
   hologram behind you. Enter `echo PERF_AWAY`, close the console, and remain
   there for 45 seconds. This is a visibility comparison, not proof that all
   hologram costs disappear when it is offscreen (shadows can still need it).
4. Combat: return to/load a save with live enemies in a slow part of the level.
   Enter `echo PERF_COMBAT`, close the console, and fight normally for 60-90
   seconds. Death/reload is okay; report it so those intervals can be excluded.
5. Enter `echo PERF_END` and exit normally. Report the tested locations/views,
   any unusual stalls, and any changes to WiVRn settings. No need to upload
   files or manually kill the collectors.

For the stationary samples, the first approximately 15 seconds after each
marker are warm-up; use complete 120-frame reporting blocks in the following
30 seconds. Discard blocks straddling markers, console/menu activity, loads,
or focus changes. If reaching a sample is impractical, report what was possible
instead of substituting a different view without noting it.

## Captured data

- `game.log` is complete stdout/stderr, also displayed in the terminal.
- `game.jsonl` adds wall-clock and monotonic receipt timestamps to each line.
  Engine console output uses stderr, so renderer reports are not delayed by
  ordinary stdout block buffering. Receipt timestamps are not GPU timestamps.
- `pidstat.log` / `.jsonl` capture per-thread CPU, scheduling/context switches,
  faults and memory for the game, WiVRn and WayVR once per second. CPU percent
  is not divided by core count: 100% means approximately one logical CPU.
- `amdgpu_top.log` / `.jsonl` capture GPU engines, clocks, thermals, memory and
  process GPU usage once per second. Media/encoding activity is not game GFX
  work. JSONL envelopes preserve the original amdgpu_top JSON in their `line`.
- `metadata.json` records commands, binary hashes, relevant settings/environment
  and active runtime executable paths. It does not export the whole environment.
- `completion.json` records exit status and collector failures. The launcher
  terminates only its own children/process groups on normal exit, failed game
  start, Ctrl-C or SIGTERM; it does not kill WiVRn or an existing collector.

## Interpretation and follow-up

At 90 Hz the nominal frame budget is 11.11 ms. Compare CPU command recording
and its BSP/light/model/skin/effect subphases against stereo GPU timestamps.
Shadow-map and mask timings help attribute GPU work without turning shadows
off in the baseline. Bloom is off; compare it separately only if useful later.

`rd-vulkan-timing wait` is CPU time in queue submission plus queue-idle wait,
NOT `xrWaitFrame`, swapchain acquisition, WiVRn network/encode/decode latency,
or presented-frame pacing. Do not add this time to GPU time as independent
costs. Model subphases are already included in the model/record parent timings.

The interval between consecutive 120-frame reports gives an effective game
submission rate only for steady, uninterrupted blocks. It is not delivered
headset FPS, and block averages/maxima cannot establish per-frame percentile
latency. Host counters can corroborate a CPU or GPU limit, not diagnose every
VR pacing issue. If there is substantial unexplained frame time, add scoped
OpenXR wait/begin/end and frontend timing before attributing it to WiVRn.

Use the result to choose one measured optimization, preserve the current
visual baseline, then repeat matching samples. Profile color mode 3 separately
with a full restart after the mode-0 baseline; it affects more than holograms.
After performance fixes are verified, inventory and supply the promised
cross-game hologram-animation test list, including exact levels and triggers.

Collector-only regression test (does not launch a game):

```bash
python3 /home/patola/workspace/codex/JKXRL-active/tools/capture_vr_performance.py --self-test
```

## Captured baseline and first optimization

Source: `/tmp/jkxrl-perf/jka-wedge-20260908-045522-hpvn6tww`, accepted renderer
JKA SHA-256 `73d3f8a75d57a8210adfe3100dc0e45306b452a3dbf9dcc8c70cbab3959b6061`.
Game exit 0, no collector errors. The amdgpu_top SIGTERM is normal cleanup.
The user starts the activity a few seconds after each echo: omit the first
15 seconds and all reporting blocks that cross the measurement boundary.
Several early commands lacked `echo`; use the later successful echo outputs,
not the resulting `Unknown command` lines. Do not mistake those for game faults.

Reproduce the analysis (relative paths from the active checkout):

```bash
python3 tools/summarize_vr_performance.py /tmp/jkxrl-perf/jka-wedge-20260908-045522-hpvn6tww
```

The result was also saved in `/tmp/jkxrl-wedge-baseline-summary.json`. Times
below are seconds from the first captured game log line. A console reopening
truncates the hologram sample; 17 complete blocks remain, which is sufficient.

| View | Used interval | Blocks | Game submissions/s | CPU record ms | GPU stereo ms | CPU model prep ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Later outdoor view | 580.846-609.491 | 17 | 71.22 | 8.62 | 4.07 | 5.08 |
| Hologram | 657.024-679.692 | 17 | 89.99 | 2.15 | 1.05 | 1.41 |
| Facing away | 706.360-735.695 | 22 | 89.99 | 1.44 | 1.01 | 0.78 |
| Combat | 425.913-483.525 | 37 | 77.07 | 7.58 | 3.94 | 3.36 |

The first outdoor sample is only 8 usable blocks (61.745-73.874 s), about
79.15 submissions/s, and has a different model workload. Keep it separate
rather than merging mismatched viewpoints into the later outdoor baseline.
These are effective game submission rates, not headset presentation FPS.

The later outdoor sample spends 1.89 ms recording BSP and 6.14 ms recording
models; the `skin` subcounter accounts for 5.08 ms of that model work. However,
the existing ranked GLM counters are negligible, compute skinning is enabled,
and CPU fallback is essentially zero. Source inspection establishes that the
same `skin` bracket also includes MD3 prop interpolation/lighting and upload.
It is not evidence that GPU character skinning stopped working.

Outdoor shadow map/mask GPU cost is about 0.049/0.343 ms, with zero skin-cache
misses for shadows. Combat map/mask is 0.037/0.231 ms. Shadows and the hologram
are not the dominant cost in these samples. No quality, visibility, LOD,
texture, color-profile, shadow, or animation changes are justified by this data.

Host evidence: later outdoor game main thread averages 64.18% of one logical
CPU, combat 62.56%, versus 22.73%/16.10% in the two room views. GFX activity
averages 43.54% outdoors and 44.30% in combat, versus about 26% in the room;
media activity stays near 74-76%. Junction temperature peaks at 67 C in the
later outdoor sample. Neither CPU percent alone nor a GPU-status flag proves
the entire frame's bottleneck: the application serially records and waits for
GPU completion, so recording cost remains important below 100% CPU usage.

`VK_StreamMD3Surface` copied the full base mesh to host-visible/coherent upload
memory, then read the mapped normals back for each vertex's diffuse lighting.
On this device, the same memory-selection policy chooses type 2, flags 6
(HOST_VISIBLE|HOST_COHERENT, not HOST_CACHED). The replacement computes the
identical pose and lighting in a CPU-local vertex and writes the completed
vertex once. It does not change the allocator, GPU synchronization, formulas,
materials, culling, mesh frames, entity transforms or draw ordering. No cache
was added; deduplicating MD3 work between eyes can be considered separately.

`tools/check_md3_upload.cpp` compares the old in-place algorithm with the
production helper in a real Vulkan mapping. At the renderer's -O1 setting,
8,192 static lit vertices took median 2.869 ms before versus 0.062 ms after;
animated lit vertices took 3.104 ms versus 0.116 ms. All output bytes match.
These are isolated CPU upload microbenchmarks, NOT predicted game-frame gains.
Cached software-driver memory does not show the same static-vertex benefit.
Boost tests cover 4,096 combinations of pose, lighting, color clamping,
interpolation and unlit attributes; GPU tests check mapped output on AMD and
Lavapipe. The ranked model timing list now includes MD3 models too (MD3
`misses` count uncached uploads), to verify attribution in the next real run.

Repeat the same collector command and route with the optimized pair. Expect
identical pictures, especially animated hologram contours, lit props, saber
illumination and character animation. Compare matching outdoor/combat views,
not the two different outdoor samples with each other. Reaching 90 Hz in the
room already leaves no FPS headroom there; look for reduced CPU work instead.

Deployment: both renderer builds and all six CTest suites passed. Installed
hashes match build outputs: JKA
`e043324c154965f262cd086de58110d03ed8c816061159f8992b616725fd9941`, JKO
`987a6e84cea27efb3704580cc0bb9f7475e8f6aa08148e5443ed472bddbab9ae`.
Previous renderers are retained in
`OpenJK/build-vulkan-clean/pre-md3-cpu-local-upload/`. No game DLLs, shaders,
asset packs or user configs were replaced for this optimization. Real-game
performance improvement and visual acceptance were pending that repeat run;
its results follow below.

## Repeat capture: improvement confirmed (2026-09-08)

Source: `/tmp/jkxrl-perf/jka-wedge-20260908-112408-eursamvz`; parsed result:
`/tmp/jkxrl-wedge-optimized-summary.json`. Game exit 0, no collector errors.
The installed JKA renderer matches the optimized hash above. Engine/game DLL
hashes and captured renderer settings match the baseline. No new Vulkan
validation errors were found. The user reports perceptibly better performance.

Use the same summarizer and settling rules as the baseline. Console reopening
truncates the outdoor/room samples; a reload truncates combat. Early console
recaptures in the room samples fall within the excluded warm-up period.

| View | Used interval | Blocks | Game submissions/s | CPU record ms | GPU stereo ms | CPU model prep ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Outdoor | 63.561-86.239 | 17 | 89.96 | 3.41 | 3.90 | 0.094 |
| Combat | 121.930-169.955 | 36 | 89.95 | 2.52 | 3.03 | 0.059 |
| Hologram | 213.024-241.026 | 21 | 89.99 | 0.67 | 0.97 | 0.050 |
| Facing away | 273.028-295.695 | 17 | 90.00 | 0.59 | 0.83 | 0.021 |

All settled samples now sustain approximately the configured 90 Hz submission
cadence. These are application submissions, not a headset presentation or
WiVRn latency measurement. The hologram room already reached 90 Hz before;
its improvement is additional CPU headroom rather than higher frame rate.

This is not a frame-identical A/B benchmark. Outdoors, BSP stage draws remain
23,904 and BSP recording stays about 1.87-1.89 ms, but visible model draws fall
from 849 to 529 with the changed view. Combat is substantially lighter: model
candidates fall from 285 to 155 and average lights from 4.34 to 0.89. Do not
attribute a precise overall speedup percentage to this optimization alone.
The byte-identical mapped-memory benchmark independently validates the targeted
cost reduction. Outdoor model preparation falls from 5.08 to 0.094 ms, while
shadow map/mask GPU costs remain essentially unchanged at 0.049/0.343 ms.
Outdoor main-thread CPU usage falls from 64.18% to 34.17% of one logical CPU;
GFX activity rises from 43.54% to 52.57%. Neither metric alone is frame latency.

No further renderer changes are warranted by this capture. Performance
acceptance is confirmed; do not treat that as an exhaustive visual or JKO
campaign pass. Continue with the promised
[cross-game hologram review](hologram-animation-review.md), retaining color
profile 0 and the accepted rendering settings.
