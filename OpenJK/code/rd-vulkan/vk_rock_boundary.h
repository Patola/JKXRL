/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <vector>

namespace vk_rock_boundary
{
using Point = std::array<float, 3>;
struct Edge
{
	Point low, high, normal;
	unsigned surface;
};
struct Join
{
	Edge a, b;
	std::array<float, 2> xy;
};

inline bool Vertical(const Edge& e)
{
	for (int i = 0; i < 3; ++i)
		if (!std::isfinite(e.low[i]) || !std::isfinite(e.high[i]) ||
			!std::isfinite(e.normal[i])) return false;
	return e.low[0] == e.high[0] && e.low[1] == e.high[1] &&
		e.high[2] - e.low[2] >= 64.0f;
}

inline bool Match(const Edge& a, const Edge& b)
{
	if (a.surface == b.surface || !Vertical(a) || !Vertical(b) ||
		a.low[2] != b.low[2] || a.high[2] != b.high[2]) return false;
	const float dx = a.low[0] - b.low[0], dy = a.low[1] - b.low[1];
	// The Rift wall has BOTH 1/8-unit and 1/2-unit boundary discontinuities.
	if ((dx != 0.0f && dy != 0.0f) || dx*dx + dy*dy == 0.0f ||
		dx*dx + dy*dy > 0.25f) return false;
	float dot = 0.0f;
	for (int i = 0; i < 3; ++i) dot += a.normal[i] * b.normal[i];
	return dot >= 0.5f;
}

inline std::vector<Join> Find(const std::vector<Edge>& edges)
{
	std::vector<unsigned> matches(edges.size());
	for (size_t i = 0; i < edges.size(); ++i)
		for (size_t j = i + 1; j < edges.size(); ++j)
			if (Match(edges[i], edges[j])) { ++matches[i]; ++matches[j]; }
	std::vector<Join> joins;
	for (size_t i = 0; i < edges.size(); ++i)
		for (size_t j = i + 1; j < edges.size(); ++j)
			if (matches[i] == 1 && matches[j] == 1 && Match(edges[i], edges[j]))
				joins.push_back({edges[i], edges[j],
					{(edges[i].low[0] + edges[j].low[0]) * 0.5f,
					 (edges[i].low[1] + edges[j].low[1]) * 0.5f}});
	return joins;
}

inline bool Apply(float position[3], const std::vector<Join>& joins)
{
	for (const auto& join : joins)
		for (const auto* edge : {&join.a, &join.b})
			if (position[0] == edge->low[0] && position[1] == edge->low[1] &&
				position[2] >= edge->low[2] && position[2] <= edge->high[2])
			{
				position[0] = join.xy[0];
				position[1] = join.xy[1];
				return true;
			}
	return false;
}

template<class Vertices, class Indices>
void Collect(std::vector<Edge>& edges, const Vertices& vertices, const Indices& indices,
	size_t first, size_t count, const Point& normal, unsigned surface)
{
	std::map<std::pair<unsigned, unsigned>, unsigned> uses;
	for (size_t i = first; i + 2 < first + count; i += 3)
		for (size_t k = 0; k < 3; ++k)
		{
			unsigned a = indices[i+k], b = indices[i+(k+1)%3];
			if (a > b) std::swap(a, b);
			++uses[{a, b}];
		}
	for (const auto& item : uses)
	{
		if (item.second != 1) continue;
		Edge edge{{}, {}, normal, surface};
		std::copy_n(vertices[item.first.first].position, 3, edge.low.begin());
		std::copy_n(vertices[item.first.second].position, 3, edge.high.begin());
		if (edge.low[2] > edge.high[2]) std::swap(edge.low, edge.high);
		if (Vertical(edge)) edges.push_back(edge);
	}
}
}
