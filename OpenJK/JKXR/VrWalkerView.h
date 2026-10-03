/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <cmath>

// Remove tracked look yaw from the chase position, not from the gun aim.
inline void JKXR_WalkerCameraOffset(float aimYaw, float headYaw,
        float referenceYaw, float hullTop, float offset[3])
{
    constexpr float radiansPerDegree = 0.017453292519943295f;
    const float yaw = (aimYaw - headYaw + referenceYaw) * radiansPerDegree;
    offset[0] = -240.0f * std::cos(yaw);
    offset[1] = -240.0f * std::sin(yaw);
    offset[2] = hullTop + 32.0f;
}
