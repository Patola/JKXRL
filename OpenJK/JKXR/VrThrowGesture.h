/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef JKXR_VR_THROW_GESTURE_H
#define JKXR_VR_THROW_GESTURE_H

#include <algorithm>
#include <array>
#include <cmath>

struct jkxr_throw_gesture_t
{
    using Vec = std::array<float, 3>;
    struct Sample
    {
        int time;
        Vec position;
        Vec velocity{};
        bool outward = false;
    };
    struct Release
    {
        Vec velocity{};
        float currentSpeed = 0;
        int strokeAgeMs = -1;
        bool assisted = false;
    };

    static float Length(const Vec &v)
    {
        return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    }
    static bool Finite(const Vec &v)
    {
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    }

    void Reset(bool held = false)
    {
        count = 0;
        wasHeld = false;
        blocked = held;
    }

    // Positions are tracking-space metres, unaffected by stick locomotion.
    // Only retain outward strokes: bringing the hand back is not a new throw.
    bool Update(int now, const Vec &position, const Vec &fromChest,
                bool held, bool allowed, int graceMs, Release &release)
    {
        release = {};
        if (!allowed || !Finite(position) || !Finite(fromChest))
        {
            Reset(held);
            return false;
        }
        if (blocked)
        {
            if (!held) Reset();
            return false;
        }
        if (!held && !wasHeld) return false;
        if (count && (now < samples[0].time || now - samples[0].time > 120))
        {
            Reset(held);
            return false;
        }
        if (count && now > samples[0].time)
        {
            Vec step{};
            for (int axis = 0; axis < 3; ++axis)
                step[axis] = (position[axis] - samples[0].position[axis]) *
                             (1000.0f / (now - samples[0].time));
            if (Length(step) > 12.0f)
            {
                Reset(held); // Tracking discontinuity, not a stronger throw.
                return false;
            }
        }
        if (!count || now != samples[0].time)
        {
            count = std::min(count + 1, int(samples.size()));
            for (int i = count - 1; i > 0; --i) samples[i] = samples[i-1];
        }
        samples[0] = {now, position};

        Vec current{};
        for (int i = 1; i < count; ++i)
        {
            const int age = now - samples[i].time;
            if (age < 50) continue;
            if (age <= 120)
                for (int axis = 0; axis < 3; ++axis)
                    current[axis] = (position[axis] - samples[i].position[axis]) *
                                    (1000.0f / age);
            break;
        }
        const float speed = Length(current);
        const float outward = current[0]*fromChest[0] + current[1]*fromChest[1] +
                              current[2]*fromChest[2];
        graceMs = std::clamp(graceMs, 0, 400);
        samples[0].velocity = current;
        samples[0].outward = speed >= 0.8f &&
                outward > 0.15f * speed * Length(fromChest);
        if (held)
        {
            wasHeld = true;
            return false;
        }
        release.velocity = current;
        release.currentSpeed = speed;
        const Sample *peak = nullptr;
        float peakSpeed = 0;
        for (int i = 0; graceMs && i < count; ++i)
        {
            if (now - samples[i].time > graceMs) break;
            const float candidateSpeed = Length(samples[i].velocity);
            if (samples[i].outward && candidateSpeed > peakSpeed)
            {
                peak = &samples[i];
                peakSpeed = candidateSpeed;
            }
        }
        if (peak)
        {
            release.velocity = peak->velocity;
            release.strokeAgeMs = now - peak->time;
            release.assisted = peak->time != now;
        }
        Reset();
        return true;
    }

private:
    std::array<Sample, 128> samples{};
    int count = 0;
    bool wasHeld = false;
    bool blocked = false;
};

// Guard the legacy fallback used by non-controller fire bindings as well.
inline void JKXR_ThermalHistoryVelocity(const float *newer, const float *older,
                                       float elapsedMs, float *velocity)
{
    for (int axis = 0; axis < 3; ++axis) velocity[axis] = 0;
    if (!std::isfinite(elapsedMs) || elapsedMs <= 0 || elapsedMs > 120) return;
    jkxr_throw_gesture_t::Vec v{};
    for (int axis = 0; axis < 3; ++axis)
        v[axis] = (newer[axis] - older[axis]) * (1000.0f / elapsedMs);
    if (!jkxr_throw_gesture_t::Finite(v) || jkxr_throw_gesture_t::Length(v) > 12) return;
    for (int axis = 0; axis < 3; ++axis) velocity[axis] = v[axis];
}

#endif
