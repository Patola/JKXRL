/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

struct jkxr_force_cast_t
{
    struct Event { bool armed = false; bool ownsTrigger = false; };
    bool held = false;
    bool owned = false;
    bool active = false;
    bool dispatched = false;
    bool gestureMode = false;

    Event Update(bool down, bool allowed, bool grip)
    {
        Event event;
        if (!allowed)
            active = false;
        if (allowed && down && !held)
        {
            active = owned = true;
            dispatched = false;
            gestureMode = grip;
            event.armed = gestureMode;
        }
        event.ownsTrigger = owned;
        if (!down)
        {
            active = owned = false;
        }
        held = down;
        return event;
    }

    bool CanGesture() const { return active && gestureMode && !dispatched; }
    bool SelectedPowerHeld() const { return active && !gestureMode; }
    void Consume() { if (active) dispatched = true; }
};
