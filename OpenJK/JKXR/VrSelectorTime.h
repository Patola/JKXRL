/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

// A wheel owns only its temporary time override, not cinematic/Force time changes.
struct jkxr_selector_time_t
{
    static constexpr float SlowScale = 0.22f;
    bool active = false;
    float previous = 1.0f;

    bool Begin(float current)
    {
        if (active)
            return false;
        active = true;
        previous = current;
        return true;
    }

    float End(float current)
    {
        const float restored = active && current == SlowScale ? previous : current;
        active = false;
        return restored;
    }
};
