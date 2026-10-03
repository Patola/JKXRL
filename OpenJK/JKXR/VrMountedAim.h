/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>

inline float JKXR_AdvanceMountedPitch(float pitch, float delta, float minimum, float maximum)
{
    return std::clamp(std::clamp(pitch, minimum, maximum) + delta, minimum, maximum);
}

struct jkxr_mounted_exit_t
{
    int owner = -1;
    int mountedAt = -1;
    int lastTime = 0;
    unsigned previous = 3;

    bool Update(int gun, int entryTime, int now, bool use, bool jump)
    {
        if (owner != gun || mountedAt != entryTime || now < lastTime)
        {
            owner = gun;
            mountedAt = entryTime;
            previous = 3; // Consume entry holds, independently for use and jump.
        }
        lastTime = now;
        const unsigned held = (use ? 1u : 0u) | (jump ? 2u : 0u);
        const unsigned pressed = held & ~previous;
        previous = held;
        return now - entryTime > 500 && pressed != 0;
    }
};

struct jkxr_mounted_stick_t
{
    int lastTime = 0;
    bool centered = false;

    static float Response(float axis)
    {
        if (!std::isfinite(axis)) return 0.0f;
        const float magnitude = std::clamp((std::fabs(axis) - 0.15f) / 0.85f, 0.0f, 1.0f);
        return std::copysign(magnitude * magnitude, axis);
    }

    void Update(float x, float y, int now, bool enabled, float yawSpeed,
            float pitchSpeed, float &yawDelta, float &pitchDelta)
    {
        const int elapsed = now - lastTime;
        const bool validTime = lastTime > 0 && elapsed > 0 && elapsed <= 100;
        lastTime = now;
        yawDelta = pitchDelta = 0.0f;
        if (!enabled)
        {
            centered = false;
            return;
        }
        if (std::isfinite(x) && std::isfinite(y) &&
                std::fabs(x) <= 0.15f && std::fabs(y) <= 0.15f)
            centered = true;
        if (!centered || !validTime) return;
        const float seconds = elapsed * 0.001f;
        yawDelta = -Response(x) * yawSpeed * seconds;
        pitchDelta = -Response(y) * pitchSpeed * seconds;
    }
};
