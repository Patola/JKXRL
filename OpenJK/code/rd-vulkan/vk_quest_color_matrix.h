#ifndef VK_QUEST_COLOR_MATRIX_H
#define VK_QUEST_COLOR_MATRIX_H

// Linear-light Rec.2020 -> linear-light sRGB, D65 white in both spaces.
// Shared by the shader and CPU tests; transfer encoding is handled separately.
#define VK_QUEST_COLOR_ROW_R 1.660491, -0.587641, -0.072850
#define VK_QUEST_COLOR_ROW_G -0.124550, 1.132900, -0.008349
#define VK_QUEST_COLOR_ROW_B -0.018151, -0.100579, 1.118730

#endif
