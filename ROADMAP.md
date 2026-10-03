# JKXRL release roadmap

Agreed 2026-10-03. These are release goals, not promised delivery dates.

| Version | Scope |
| --- | --- |
| **0.6** | Public, playable Linux x86-64 PCVR release. Vulkan + SDL3 + OpenXR; installable Arch package and binary archives. |
| **0.6.x / 0.7.x** | Bug fixes and measured, general optimizations informed by ARM/mobile constraints, while keeping PC support and accepted rendering intact. |
| **0.8** | First native ARM64 port attempt, targeting Steam Frame when hardware and the verified development environment are available. No FEX requirement. |
| **0.8.x / 0.9.x** | ARM64 fixes, hardware-specific optimization, complete gameplay testing and PC regressions. |
| **1.0** | Validated Steam Frame gameplay, stability and sustained performance, alongside the PC release. |
| **1.0.x through 1.2** | Deliberate additions using previously unused effects: candidate sand effects in outdoor Tatooine/Ragnos areas and ice effects on Hoth, evaluated for gameplay and performance. |

The 1.2 effects are proposed enhancements, not claims that those effects are
missing from the original campaign. Review their assets, placement, controls
and performance only after 1.0 gameplay on Frame is confirmed. Full physical
ragdolls and other recorded extras remain separately scoped, not silently
promised for 1.0.

## Optimization rules

- Freeze tested checkpoints before risky changes; retain source tags and release artifacts.
- Measure each game separately with repeatable scene/save/viewpoint captures.
- Keep visual correctness, VR comfort and input latency as acceptance gates.
- Desktop CPU/GPU timings do not predict ARM FPS. Verify memory/driver/thermal behavior on hardware.
- No blanket aggressive compiler flags or feature disabling presented as equivalent-quality optimization.

Technical plan and benchmark locations:
[ARM-aware optimization](OpenJK/docs/optimization-arm-readiness.md).

This numbering supersedes earlier tentative v0.7/v0.8 ARM dates or open-ended
feature lists. Remaining high white subtitles, camera help timeout and
localized explosion performance remain recorded follow-ups.
