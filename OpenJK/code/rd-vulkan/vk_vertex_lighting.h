#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace vk_vertex_lighting {
using Color = std::array<uint8_t, 4>;
using Colors = std::array<Color, 4>;
using Styles = std::array<uint8_t, 4>;
using Palette = std::array<Color, 64>;

// ComputeFinalVertexColor's integer sum, then shift/clamp, not additive draws.
inline Color Combine(const Colors& colors, const Styles& styles, const Palette& palette)
{
    Color result = colors[0];
    unsigned sum[3]{};
    for (std::size_t slot = 0; slot < styles.size() && styles[slot] < palette.size(); ++slot)
        for (int c = 0; c < 3; ++c)
            sum[c] += unsigned(colors[slot][c]) * palette[styles[slot]][c];
    for (int c = 0; c < 3; ++c) result[c] = uint8_t(std::min(255u, sum[c] >> 8));
    return result; // Original alpha, never scaled by a light style.
}

inline bool Changed(const Styles& styles, const Palette& before, const Palette& after)
{
    for (auto style : styles)
    {
        if (style >= after.size()) break;
        if (before[style] != after[style]) return true;
    }
    return false;
}
}
