/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef VK_CONSOLE_PROJECTION_H
#define VK_CONSOLE_PROJECTION_H

#include <cmath>

// Preserve signed W. The GPU clips partially visible rectangles and performs
// perspective-correct interpolation; rejecting one corner loses the whole key.
inline bool VK_ConsoleClipFinite( const float clip[3] )
{
	return std::isfinite( clip[0] ) && std::isfinite( clip[1] ) && std::isfinite( clip[2] );
}

inline bool VK_ConsoleGameClipPoint( const float matrix[16], const float point[3], float clip[3] )
{
	const int rows[] = { 0, 1, 3 };
	for ( int i = 0; i < 3; ++i )
	{
		const int row = rows[i];
		clip[i] = matrix[row] * point[0] + matrix[row + 4] * point[1] +
			matrix[row + 8] * point[2] + matrix[row + 12];
	}
	return VK_ConsoleClipFinite( clip );
}

inline bool VK_ConsoleViewClipPoint(
	float x, float y, float z, float tanLeft, float tanRight, float tanDown, float tanUp,
	float clip[3] )
{
	const float width = tanRight - tanLeft;
	const float height = tanUp - tanDown;
	if ( width <= 0.0001f || height <= 0.0001f ) return false;
	clip[2] = -z;
	clip[0] = ( 2.0f * x - ( tanLeft + tanRight ) * clip[2] ) / width;
	// Positive Vulkan viewport height: negative NDC Y is the top.
	clip[1] = ( -2.0f * y + ( tanDown + tanUp ) * clip[2] ) / height;
	return VK_ConsoleClipFinite( clip );
}

#endif
