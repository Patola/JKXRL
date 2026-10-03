/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cmath>

// Font widths are already measured at the final glyph scale, but the draw
// wrapper still transforms their origin. Undo that scale for centering only.
inline int VR_CenteredTextOrigin(float center, float pixelWidth, float originScale)
{
    if (!std::isfinite(originScale) || originScale <= 0) originScale = 1;
    return static_cast<int>(std::lround(center - pixelWidth / (2 * originScale)));
}
