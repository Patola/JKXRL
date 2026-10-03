/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>

enum class VrForceHintTarget { None, Door, Static };

inline bool VR_ForceHintEligible(VrForceHintTarget target, int flags,
    float pushRange, float pullRange, float distance)
{
    if (!std::isfinite(distance) || distance < 0) return false;
    bool push = false, pull = false;
    if (target == VrForceHintTarget::Door && (flags & 2)) push = pull = true;
    if (target == VrForceHintTarget::Static)
    {
        push = (flags & 1) != 0;
        pull = (flags & 2) != 0;
    }
    return (push && pushRange > 0 && distance <= pushRange) ||
        (pull && pullRange > 0 && distance <= pullRange);
}

inline float VR_ForceHintRadius(float size, float fov, float distance)
{
    // A depth-tested world sprite; cap angular growth near the hand and avoid
    // giant markers on distant brush faces. Independent of render-eye offsets.
    if (!std::isfinite(size) || !std::isfinite(fov) || !std::isfinite(distance)) return 2;
    return std::clamp(std::clamp(size, 1.0f, 64.0f) / 640.0f *
        std::tan(std::clamp(fov, 1.0f, 160.0f) * 0.00872664626f) * distance * 2.0f,
        2.0f, 24.0f);
}
