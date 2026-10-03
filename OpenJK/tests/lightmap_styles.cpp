/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_coplanar_overlap.h"

BOOST_AUTO_TEST_SUITE(LightmapStyles)

BOOST_AUTO_TEST_CASE(SharedTriangleEdgesMustRemainBatchable)
{
	const vk_triangle2_t a{{{{0,0}},{{10,0}},{{0,10}}}};
	const vk_triangle2_t neighbor{{{{10,0}},{{10,10}},{{0,10}}}};
	BOOST_CHECK(!VK_TrianglesOverlap(a, neighbor));
	BOOST_CHECK(VK_TrianglesOverlap(a, a));
	const vk_triangle2_t inside{{{{1,1}},{{2,1}},{{1,2}}}};
	BOOST_CHECK(VK_TrianglesOverlap(a, inside));
	const vk_triangle2_t degenerate{{{{1,1}},{{1,1}},{{1,1}}}};
	BOOST_CHECK(!VK_TrianglesOverlap(a, degenerate));
	auto reversed = inside;
	std::swap(reversed[0], reversed[2]);
	BOOST_CHECK(VK_TrianglesOverlap(reversed, a));
	const vk_triangle2_t far{{{{11,11}},{{12,11}},{{11,12}}}};
	BOOST_CHECK(!VK_TrianglesOverlap(a, far));
}

BOOST_AUTO_TEST_SUITE_END()
