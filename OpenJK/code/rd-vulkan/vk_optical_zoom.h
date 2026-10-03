/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>

inline float VK_OpticalZoomScale(bool active, float requestedFov, float headsetFov)
{
	if (!active || !std::isfinite(requestedFov) || !std::isfinite(headsetFov) ||
		requestedFov <= 0 || headsetFov <= 0 || headsetFov >= 180 || requestedFov >= headsetFov)
		return 1.0f;
	constexpr float degreesToHalfRadians = 3.14159265358979323846f / 360.0f;
	const float headsetTangent = std::tan(headsetFov * degreesToHalfRadians);
	const float zoomTangent = std::tan(requestedFov * degreesToHalfRadians);
	return headsetTangent > 0 ? std::clamp(zoomTangent / headsetTangent, 0.01f, 1.0f) : 1.0f;
}
