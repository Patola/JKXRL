/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct vk_view_fog_snapshot_t
{
	bool goggles = false;
	float range = 0;
};

struct vk_view_fog_state_t
{
	bool goggles = false;
	float range = 0;
	void ClearScene() { goggles = false; }
	void BeginFrame() { goggles = false; range = 0; }
	void SetRange(float value) { if (std::isfinite(value)) range = value; }
	vk_view_fog_snapshot_t Snapshot(bool world) const
	{
		return world ? vk_view_fog_snapshot_t{goggles, range} : vk_view_fog_snapshot_t{};
	}
};

struct vk_view_fog_t
{
	bool enabled = false;
	bool linearRange = false;
	float start = 0;
	float end = 1;
	std::array<float, 3> color{};
};

inline vk_view_fog_t VK_ViewFog(const vk_view_fog_snapshot_t &view,
	bool hasFog, const float color[3], float depth, float distanceCull,
	float authoredRange, int time)
{
	vk_view_fog_t result;
	result.enabled = hasFog || view.goggles;
	if (!result.enabled) return result;
	result.color = {color[0], color[1], color[2]};
	result.end = std::isfinite(depth) && depth > 0 ? depth : 1;
	if (view.goggles)
	{
		// Legacy warm fog; deterministic per scene time, never per-eye RNG.
		uint32_t noise = static_cast<uint32_t>(time);
		noise ^= noise >> 16; noise *= 0x7feb352du; noise ^= noise >> 15;
		result.color = {0.75f, 0.42f + (noise & 65535u) * (0.025f / 65535), 0.07f};
		result.end = 10000;
	}
	const float range = view.range != 0 ? view.range : authoredRange;
	if (range < 0 && std::isfinite(range))
	{
		result.linearRange = true;
		result.start = std::min(-range, result.end - 1);
	}
	else if (range > 0 && std::isfinite(range))
	{
		result.linearRange = true;
		const float cull = std::isfinite(distanceCull) && distanceCull > 16 ? distanceCull : 12000;
		result.start = std::max(16.0f, std::min(result.end, cull - range));
		result.end = cull;
		result.start = std::min(result.start, result.end - 1);
	}
	return result;
}
