/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <array>
#include <algorithm>
#include <cmath>

using vk_triangle2_t = std::array<std::array<double, 2>, 3>;

// Strict positive-area intersection: shared BSP triangle edges are not overlap.
inline bool VK_TrianglesOverlap(const vk_triangle2_t& a, const vk_triangle2_t& b)
{
	for (const auto* triangle : {&a, &b})
	{
		const auto& t = *triangle;
		if (std::fabs((t[1][0]-t[0][0])*(t[2][1]-t[0][1]) -
			(t[1][1]-t[0][1])*(t[2][0]-t[0][0])) < 0.0001) return false;
		for (int i = 0; i < 3; ++i)
		{
			const double x = t[(i+1)%3][1]-t[i][1], y = t[i][0]-t[(i+1)%3][0];
			const double length = std::hypot(x, y);
			double amin = a[0][0]*x+a[0][1]*y, amax = amin;
			double bmin = b[0][0]*x+b[0][1]*y, bmax = bmin;
			for (int v = 1; v < 3; ++v)
			{
				const double ap = a[v][0]*x+a[v][1]*y, bp = b[v][0]*x+b[v][1]*y;
				amin = std::min(amin, ap); amax = std::max(amax, ap);
				bmin = std::min(bmin, bp); bmax = std::max(bmax, bp);
			}
			if (std::min(amax,bmax)-std::max(amin,bmin) <= length*0.0001) return false;
		}
	}
	return true;
}
