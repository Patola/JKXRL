#pragma once

#include <array>

// Shader-script matrix order: s' = s*m00 + t*m10 + translateS.
using vk_texture_transform_t = std::array<float, 6>;

inline void VK_TransformTextureCoordinate(float uv[2], const vk_texture_transform_t &m)
{
	const float s = uv[0], t = uv[1];
	uv[0] = s * m[0] + t * m[2] + m[4];
	uv[1] = s * m[1] + t * m[3] + m[5];
}
