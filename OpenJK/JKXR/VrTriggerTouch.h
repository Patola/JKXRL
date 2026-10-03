/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <array>
#include <cstddef>

inline bool VR_IsUseButtonTrigger(bool multiTouch, int spawnflags)
{
    // Flag bits belong to each trigger class: 4 means LINEAR on trigger_push.
    return multiTouch && (spawnflags & 4) != 0;
}

template<size_t Count>
struct VrTriggerTouchSet
{
    std::array<bool, Count> visited = {};
    bool Seen(int entity) const
    {
        return entity < 0 || static_cast<size_t>(entity) >= Count || visited[entity];
    }
    void Mark(int entity)
    {
        if (entity >= 0 && static_cast<size_t>(entity) < Count) visited[entity] = true;
    }
};
