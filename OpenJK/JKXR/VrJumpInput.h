/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef JKXR_VR_JUMP_INPUT_H
#define JKXR_VR_JUMP_INPUT_H

// Return only owned command edges; controller snapshots may be consumed by UI.
struct jkxr_jump_input_t
{
    bool held = false;
    bool requireRelease = false;
    bool wasCamera = false;

    int Update(bool down, bool allowed, bool camera)
    {
        if (!allowed || camera != wasCamera)
        {
            requireRelease = down;
        }
        wasCamera = camera;
        if (!down)
        {
            requireRelease = false;
        }
        const bool next = allowed && down && !requireRelease;
        const int edge = next == held ? 0 : (next ? 1 : -1);
        held = next;
        return edge;
    }
};

#endif
