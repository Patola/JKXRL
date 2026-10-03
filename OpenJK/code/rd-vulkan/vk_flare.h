/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "vk_billboard.h"

namespace vk_flare {
using vk_billboard::Point;
struct Geometry {
	std::array<Point, 4> points{};
	float radius = 0;
	unsigned char intensity = 0;
};

// RB_SurfaceFlare's offset, distance-size curve and two-sided angular response.
// The shared scene axes, never a per-eye origin, determine the quad.
inline bool Build(Point origin, Point normal, Point eye, Point left, Point up,
	float authoredRadius, Geometry& output)
{
	using namespace vk_billboard;
	output = {};
	for (Point p : {origin, normal, eye, left, up})
		for (float v : p) if (!std::isfinite(v)) return false;
	if (!Normalize(left) || !Normalize(up) || std::fabs(Dot(left, up)) > 0.001f)
		return false;
	if (!std::isfinite(authoredRadius) || authoredRadius <= 0) authoredRadius = 30;
	origin = Add(origin, Scale(normal, 3));
	Point direction = Sub(origin, eye);
	const float distance = std::sqrt(Dot(direction, direction));
	if (!std::isfinite(distance) || distance < 0.0001f) return false;
	direction = Scale(direction, 1 / distance);
	output.intensity = static_cast<unsigned char>(255 * std::clamp(std::fabs(Dot(direction, normal)), 0.0f, 1.0f));
	output.radius = std::max(5.0f, authoredRadius * std::min(distance / 512.0f, 1.0f));
	left = Scale(left, output.radius);
	up = Scale(up, output.radius);
	output.points = {Add(Add(origin, left), up), Add(Sub(origin, left), up),
		Sub(Sub(origin, left), up), Sub(Add(origin, left), up)};
	for (const auto& p : output.points)
		for (float value : p) if (!std::isfinite(value)) { output = {}; return false; }
	return true;
}
}
