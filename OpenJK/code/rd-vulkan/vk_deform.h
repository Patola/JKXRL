// Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later.
#pragma once
#include "vk_waveform.h"
#include <array>
#include <vector>

enum vk_deform_type_t { VK_DEFORM_WAVE = 1, VK_DEFORM_BULGE, VK_DEFORM_MOVE };
struct vk_deform_t
{
	vk_deform_type_t type = VK_DEFORM_WAVE;
	vk_waveform_t function = VK_WAVE_SIN;
	float spread = 0;
	std::array<float, 4> wave{};
	std::array<float, 3> vector{}; // move XYZ, or bulge width/height/speed
};
constexpr size_t VK_MAX_DEFORMS = 3;

// Legacy planar-face selection, with room for deformation near the plane.
// Use the shared scene eye and authored BSP plane, never triangle winding.
inline bool VK_DeformFaceVisible(float signedDistance, bool backSided, float extent, bool flatCutout = false)
{
	const float margin = flatCutout ? 0.0f : 8.0f + std::max(0.0f, extent);
	return backSided ? signedDistance <= margin : signedDistance >= -margin;
}

struct vk_deform_block_t
{
	std::array<float, 4> control{}; // count, shader time, scene time, billboard enabled
	std::array<std::array<float, 4>, VK_MAX_DEFORMS> meta{}, wave{}, vector{};
	std::array<std::array<float, 4>, 4> billboard{};
	std::array<float, 4> specularLight{}; // model-local direction; W=1 for entity lighting
};
static_assert(sizeof(vk_deform_block_t) == 240, "std140 deformation/material ABI");

inline vk_deform_block_t VK_DeformBlock(const std::vector<vk_deform_t>& stages,
	float seconds, float shaderTime)
{
	vk_deform_block_t block{};
	block.control = {static_cast<float>(std::min(stages.size(), VK_MAX_DEFORMS)),
		seconds - shaderTime, seconds, 0};
	for (size_t i = 0; i < std::min(stages.size(), VK_MAX_DEFORMS); ++i)
	{
		const auto& d = stages[i];
		block.meta[i] = {static_cast<float>(d.type), static_cast<float>(d.function), d.spread, 0};
		block.wave[i] = d.wave;
		block.vector[i] = {d.vector[0], d.vector[1], d.vector[2], 0};
	}
	return block;
}

// Match legacy lookup-table truncation, including negative phases.
inline float VK_DeformWave(vk_waveform_t function, float phase)
{
	const int index = static_cast<int>(std::fmod(std::trunc(phase * 1024.0f), 1024.0f)) & 1023;
	if (function == VK_WAVE_SIN)
		return std::sin(index * (6.28318530717958647692f / 1023.0f));
	return VK_EvaluateWaveform(function, index / 1024.0f);
}

inline std::array<float, 3> VK_DeformPosition(const vk_deform_block_t& block,
	std::array<float, 3> position, const std::array<float, 3>& normal, float u)
{
	if(block.control[3] != 0)
	{
		const auto p=position;
		for(int i=0;i<3;++i) position[i]=block.billboard[i][0]*p[0]+block.billboard[i][1]*p[1]+
			block.billboard[i][2]*p[2]+block.billboard[i][3];
	}
	for (int i = 0; i < static_cast<int>(block.control[0]); ++i)
	{
		const auto& m = block.meta[i];
		const auto& w = block.wave[i];
		const auto& v = block.vector[i];
		float value;
		if (m[0] == VK_DEFORM_BULGE)
		{
			value = v[0] == 0 && v[2] == 0 ? v[1] : v[1] * VK_DeformWave(VK_WAVE_SIN,
				(u * v[0] + block.control[2] * v[2]) / 6.28318530717958647692f);
		}
		else
		{
			const float offset = m[0] == VK_DEFORM_WAVE && w[3] != 0
				? (position[0] + position[1] + position[2]) * m[2] : 0;
			value = w[0] + w[1] * VK_DeformWave(static_cast<vk_waveform_t>(static_cast<int>(m[1])),
				w[2] + block.control[1] * w[3] + offset);
		}
		for (int axis = 0; axis < 3; ++axis)
			position[axis] += value * (m[0] == VK_DEFORM_MOVE ? v[axis] : normal[axis]);
	}
	return position;
}

inline float VK_DeformExtent(const vk_deform_t& deform)
{
	if (deform.type == VK_DEFORM_BULGE) return std::fabs(deform.vector[1]);
	const float wave = std::fabs(deform.wave[0]) + std::fabs(deform.wave[1]);
	if (deform.type != VK_DEFORM_MOVE) return wave;
	return wave * std::max({std::fabs(deform.vector[0]), std::fabs(deform.vector[1]), std::fabs(deform.vector[2])});
}
