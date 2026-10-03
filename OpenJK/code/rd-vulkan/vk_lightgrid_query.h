/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "../qcommon/q_shared.h"
#include "../qcommon/qfiles.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Gameplay query, not entity shading: no minimum ambient, dynamic lights,
// palette conversion or entity-local direction. BSP bytes use the Quest's
// default map-overbright=0; ambient/direct scales are 0.5/1.0.
inline bool VK_QueryLightGrid(
    const float *origin, const float *gridOrigin, const float *gridSize,
    const int *bounds, const std::vector<dgrid_t> &data,
    const std::vector<uint16_t> &indices,
    const std::array<std::array<byte, 4>, MAX_LIGHT_STYLES> &styles,
    const float *sun, float *ambient, float *directed, float *direction)
{
    float sunLength = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        ambient[axis] = directed[axis] = 255.0f;
        sunLength += sun[axis] * sun[axis];
    }
    for (int axis = 0; axis < 3; ++axis)
        direction[axis] = std::isfinite(sunLength) && sunLength > 0.000001f
            ? sun[axis] / std::sqrt(sunLength) : (axis == 2 ? 1.0f : 0.0f);
    if (!origin || data.empty() || indices.empty())
        return false;

    int position[3];
    float fraction[3];
    size_t count = 1;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (bounds[axis] <= 0 || !std::isfinite(origin[axis]) ||
            !std::isfinite(gridOrigin[axis]) || !std::isfinite(gridSize[axis]) ||
            gridSize[axis] <= 0 || count > std::numeric_limits<size_t>::max() / bounds[axis])
            return false;
        count *= bounds[axis];
        const double coordinate = std::clamp(
            (static_cast<double>(origin[axis]) - gridOrigin[axis]) / gridSize[axis],
            0.0, static_cast<double>(bounds[axis] - 1));
        position[axis] = static_cast<int>(coordinate);
        fraction[axis] = static_cast<float>(coordinate - position[axis]);
    }
    if (count != indices.size())
        return false;

    float a[3] = {}, d[3] = {}, n[3] = {}, total = 0.0f;
    for (int corner = 0; corner < 8; ++corner)
    {
        float weight = 1.0f;
        size_t index = 0, stride = 1;
        for (int axis = 0; axis < 3; ++axis)
        {
            const bool upper = (corner & (1 << axis)) != 0;
            weight *= upper ? fraction[axis] : 1.0f - fraction[axis];
            index += std::min(position[axis] + int(upper), bounds[axis] - 1) * stride;
            stride *= bounds[axis];
        }
        if (weight <= 0)
            continue;
        if (indices[index] >= data.size())
            return false;
        const dgrid_t &sample = data[indices[index]];
        if (sample.styles[0] == LS_NONE)
            continue;
        total += weight;
        for (int style = 0; style < MAXLIGHTMAPS && sample.styles[style] != LS_NONE; ++style)
        {
            const byte id = sample.styles[style];
            if (id >= styles.size())
                continue;
            for (int channel = 0; channel < 3; ++channel)
            {
                const float scale = weight * styles[id][channel] / 255.0f;
                a[channel] += sample.ambientLight[style][channel] * scale;
                d[channel] += sample.directLight[style][channel] * scale;
            }
        }
        constexpr float byteAngle = 6.283185307179586f / 256.0f;
        const float latitude = sample.latLong[1] * byteAngle;
        const float longitude = sample.latLong[0] * byteAngle;
        n[0] += weight * std::cos(latitude) * std::sin(longitude);
        n[1] += weight * std::sin(latitude) * std::sin(longitude);
        n[2] += weight * std::cos(longitude);
    }
    const float normalize = total > 0 && total < 0.99f ? 1.0f / total : 1.0f;
    const float length = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    for (int axis = 0; axis < 3; ++axis)
    {
        ambient[axis] = a[axis] * normalize * 0.5f;
        directed[axis] = d[axis] * normalize;
        if (length > 0.000001f)
            direction[axis] = n[axis] / length;
    }
    return true;
}
