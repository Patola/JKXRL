// Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later.
#pragma once

#include <algorithm>
#include <cmath>

inline float VK_VegetationDistanceScale( float scale )
{
	return std::isfinite( scale ) ? std::clamp( scale, 1.0f, 4.0f ) : 3.0f;
}

inline float VK_VegetationCoverage(
	float distance, float fadeStart, float fadeEnd, float phase )
{
	const float fraction = ( fadeEnd - distance ) / std::max( 1.0f, fadeEnd - fadeStart );
	const float randomStart = 0.55f + 0.45f * ( 0.5f + 0.5f * std::sin( phase * 7.0f ) );
	// Normalize on both sides of randomStart: conditional division jumps at the boundary.
	const float alpha = std::clamp( fraction / randomStart, 0.0f, 1.0f );
	return alpha * alpha * ( 3.0f - 2.0f * alpha );
}
