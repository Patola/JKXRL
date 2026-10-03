/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

inline size_t VK_ShaderAnimationFrame(size_t count, float seconds, float speed,
    bool oneShot, int explicitFrame = -1)
{
    if (count <= 1) return 0;
    // RF_SETANIMINDEX is a per-entity selector, including frame zero. It must
    // bypass the clock without mutating the shared material's animation state.
    double frame = explicitFrame;
    if (explicitFrame < 0)
    {
        const float timedFrame = seconds * std::max(0.0f, speed);
        frame = std::isfinite(timedFrame) ? std::floor(timedFrame) : 0.0;
    }
    frame = std::max(0.0, frame);
    return oneShot ? static_cast<size_t>(std::min(frame, double(count - 1)))
        : static_cast<size_t>(std::fmod(frame, double(count)));
}
