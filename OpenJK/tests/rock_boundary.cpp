/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_rock_boundary.h"
#include <limits>

using namespace vk_rock_boundary;

BOOST_AUTO_TEST_CASE(rift_two_edges_not_only_eighth_unit)
{
	const std::vector<Edge> edges{
		{{-1740.5f,1544,-5056},{-1740.5f,1544,4928},{1,0,0},9135},
		{{-1740,1544,-5056},{-1740,1544,4928},{1,0,0},9139},
		{{-1770,1878,-5056},{-1770,1878,4928},{1,0,0},9138},
		{{-1770.125f,1878,-5056},{-1770.125f,1878,4928},{1,0,0},9139}
	};
	const auto joins = Find(edges);
	BOOST_REQUIRE_EQUAL(joins.size(), 2);
	for (const auto& edge : edges)
		for (float z : {edge.low[2], 0.0f, edge.high[2]})
		{
			float p[]{edge.low[0], edge.low[1], z};
			BOOST_CHECK(Apply(p, joins));
			BOOST_CHECK_EQUAL(p[2], z);
			BOOST_CHECK_LE(std::hypot(p[0]-edge.low[0],p[1]-edge.low[1]), 0.25f);
			BOOST_CHECK_EQUAL(p[0], edge.low[1] == 1544 ? -1740.25f : -1770.0625f);
			BOOST_CHECK(!Apply(p, joins));
		}
	float outside[]{-1740,1544,4929};
	BOOST_CHECK(!Apply(outside, joins));
}

BOOST_AUTO_TEST_CASE(rift_boundary_rejects_unrelated_edges)
{
	const Edge a{{0,0,0},{0,0,100},{1,0,0},1};
	const Edge b{{.5f,0,0},{.5f,0,100},{1,0,0},2};
	BOOST_CHECK(Match(a,b));
	for (int variant = 0; variant < 8; ++variant)
	{
		Edge other = b;
		if (variant == 0) other.low[0] = other.high[0] = 0.501f;
		if (variant == 1) other.high[2] = 99;
		if (variant == 2) other.low[1] = other.high[1] = 0.125f;
		if (variant == 3) other.normal = {-1,0,0};
		if (variant == 4) other.surface = 1;
		if (variant == 5) other.low[0] = other.high[0] = 0;
		if (variant == 6) other.low[0] = std::numeric_limits<float>::quiet_NaN();
		if (variant == 7) other.high[0] += .1f;
		BOOST_CHECK(!Match(a,other));
	}
	Edge duplicate = b; duplicate.surface = 3;
	BOOST_CHECK(Find({a,b,duplicate}).empty());
}

BOOST_AUTO_TEST_CASE(rift_boundary_collection_preserves_attributes)
{
	struct Vertex { float position[3]; float uv[2]; unsigned color; };
	std::vector<Vertex> vertices{
		{{0,0,0},{.1f,.2f},1},{{0,0,100},{.3f,.4f},2},{{-10,0,0},{.5f,.6f},3},
		{{.5f,0,0},{.7f,.8f},4},{{.5f,0,100},{.9f,1},5},{{10,0,0},{.2f,.3f},6}
	};
	const auto before = vertices;
	const std::vector<unsigned> indices{0,1,2,3,5,4};
	std::vector<Edge> edges;
	Collect(edges,vertices,indices,0,3,{0,1,0},1);
	Collect(edges,vertices,indices,3,3,{0,1,0},2);
	const auto joins = Find(edges);
	BOOST_REQUIRE_EQUAL(joins.size(),1);
	for (size_t i=0;i<vertices.size();++i)
	{
		Apply(vertices[i].position,joins);
		BOOST_CHECK_EQUAL(vertices[i].position[2],before[i].position[2]);
		BOOST_CHECK_EQUAL(vertices[i].uv[0],before[i].uv[0]);
		BOOST_CHECK_EQUAL(vertices[i].uv[1],before[i].uv[1]);
		BOOST_CHECK_EQUAL(vertices[i].color,before[i].color);
	}
	BOOST_CHECK_EQUAL(vertices[0].position[0],.25f);
	BOOST_CHECK_EQUAL(vertices[3].position[0],.25f);
	BOOST_CHECK_EQUAL(vertices[2].position[0],-10);
	BOOST_CHECK_EQUAL(vertices[5].position[0],10);
}
