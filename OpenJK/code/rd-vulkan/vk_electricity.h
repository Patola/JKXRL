// Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later.
#pragma once

#include "qcommon/q_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

struct vk_electricity_t
{
	vec3_t start{}, end{};
	float radius = 1, chaos = 1, growth = 1;
	int seed = 0;
	bool tapered = false, forked = false;
};

struct vk_electricity_segment_t
{
	vec3_t start{}, end{};
	float startRadius = 0, endRadius = 0, startUV = 0, endUV = 0;
	int strand = 0;
};

struct vk_electricity_stats_t
{
	int forks = 0, segments = 0;
};

inline float VK_ElectricityRandom(uint32_t &seed)
{
	// Q_random's recurrence, with defined unsigned wrapping and no global state.
	seed = 69069u * seed + 1u;
	return (seed & 0xffffu) / 65536.0f;
}

namespace vk_electricity_detail
{
template<class Emit>
void Bolt(const vec3_t start, const vec3_t end, float radius, float phaseSeed,
	int strand, const vk_electricity_t &effect, const vec3_t rootEnd,
	uint32_t &random, vk_electricity_stats_t &stats, Emit &emit)
{
	vec3_t direction, side, vertical;
	VectorSubtract(end, start, direction);
	const float distance = VectorNormalize(direction);
	if (!std::isfinite(distance) || distance <= 0.001f)
		return;
	MakeNormalVectors(direction, side, vertical);
	const int segments = static_cast<int>(std::clamp(distance / 16.0f, 1.0f, 64.0f));
	vec3_t previous;
	VectorCopy(start, previous);
	for (int i = 1; i <= segments; ++i)
	{
		vk_electricity_segment_t line;
		line.strand = strand;
		line.startUV = static_cast<float>(i - 1) / segments;
		line.endUV = static_cast<float>(i) / segments;
		VectorCopy(previous, line.start);
		VectorMA(start, distance * line.endUV, direction, line.end);
		if (i < segments)
		{
			const float envelope = std::sin(line.endUV * 3.14159265359f);
			const float phase = phaseSeed * 0.00031f + i * 2.39996323f;
			VectorMA(line.end, std::sin(phase) * effect.chaos * 7.0f * envelope, side, line.end);
			VectorMA(line.end, std::cos(phase * 1.37f) * effect.chaos * 7.0f * envelope, vertical, line.end);
		}
		line.startRadius = effect.tapered ? radius * (1 - line.startUV * line.startUV) : radius;
		line.endRadius = effect.tapered ? radius * (1 - line.endUV * line.endUV) : radius;
		emit(line);
		++stats.segments;

		// Legacy RF_FORKED: 7% chance, first fifth only, three forks total
		// including recursive forks. The existing Vulkan trunk stays unchanged.
		if (effect.forked && stats.forks < 3 && VK_ElectricityRandom(random) > 0.93f &&
			(1.0f - line.endUV) > 0.8f)
		{
			vec3_t target;
			for (int axis = 0; axis < 3; ++axis)
				target[axis] = (line.end[axis] + rootEnd[axis]) * 0.5f +
					(2 * VK_ElectricityRandom(random) - 1) * 80.0f;
			const int child = ++stats.forks;
			Bolt(line.end, target, line.endRadius, static_cast<float>(random & 0xffffu),
				child, effect, rootEnd, random, stats, emit);
		}
		VectorCopy(line.end, previous);
	}
}
}

template<class Emit>
vk_electricity_stats_t VK_BuildElectricity(const vk_electricity_t &effect, Emit emit)
{
	vk_electricity_stats_t stats;
	if (!std::isfinite(effect.radius) || effect.radius <= 0 ||
		!std::isfinite(effect.chaos) || !std::isfinite(effect.growth))
		return stats;
	for (int axis = 0; axis < 3; ++axis)
		if (!std::isfinite(effect.start[axis]) || !std::isfinite(effect.end[axis]))
			return stats;
	vec3_t direction, end;
	VectorSubtract(effect.end, effect.start, direction);
	const float distance = VectorNormalize(direction);
	if (!std::isfinite(distance) || distance <= 0.001f)
		return stats;
	VectorMA(effect.start, distance * std::clamp(effect.growth, 0.0f, 1.0f), direction, end);
	uint32_t random = static_cast<uint32_t>(effect.seed);
	vk_electricity_detail::Bolt(effect.start, end, effect.radius, static_cast<float>(effect.seed),
		0, effect, end, random, stats, emit);
	return stats;
}
