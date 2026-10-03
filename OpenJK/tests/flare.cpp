#include <boost/test/unit_test.hpp>
#include <limits>
#include "../code/rd-vulkan/vk_flare.h"

using namespace vk_flare;
using namespace vk_billboard;

BOOST_AUTO_TEST_SUITE(material_flares)
BOOST_AUTO_TEST_CASE(legacy_size_curve_and_offset)
{
	for (float distance : {1.0f, 32.0f, 256.0f, 512.0f, 2048.0f})
	{
		Geometry g;
		BOOST_REQUIRE(Build({0,0,0}, {1,0,0}, {3+distance,0,0}, {0,1,0}, {0,0,1}, 50, g));
		BOOST_CHECK_SMALL(g.radius - std::max(5.0f, 50.0f * std::min(distance / 512, 1.0f)), 0.0001f);
		Point center{};
		for (const auto& p : g.points) center = Add(center, Scale(p, 0.25f));
		BOOST_CHECK_SMALL(center[0] - 3, 0.0001f);
		BOOST_CHECK_SMALL(center[1], 0.0001f);
		BOOST_CHECK_SMALL(center[2], 0.0001f);
		BOOST_CHECK_EQUAL(g.intensity, 255);
	}
}
BOOST_AUTO_TEST_CASE(two_sided_angular_fade_and_radius_fallback)
{
	Geometry front, back, edge;
	BOOST_REQUIRE(Build({0,0,0}, {1,0,0}, {1027,0,0}, {0,1,0}, {0,0,1}, 0, front));
	BOOST_REQUIRE(Build({0,0,0}, {1,0,0}, {-1021,0,0}, {0,-1,0}, {0,0,1}, 0, back));
	BOOST_REQUIRE(Build({0,0,0}, {1,0,0}, {3,1024,0}, {1,0,0}, {0,0,1}, 0, edge));
	BOOST_CHECK_EQUAL(front.intensity, back.intensity);
	BOOST_CHECK_EQUAL(edge.intensity, 0);
	BOOST_CHECK_EQUAL(front.radius, 30);
}
BOOST_AUTO_TEST_CASE(shared_scene_geometry_is_stereo_stable)
{
	// A stock Hoth flare center; no eye offset is passed to the generator.
	for (int angle = 0; angle < 360; ++angle)
	{
		float a = angle * 0.0174532925f;
		const Point eye{1124+500*std::cos(a), -7716+500*std::sin(a), 957};
		const Point left{-std::sin(a), std::cos(a), 0};
		Geometry g, again;
		BOOST_REQUIRE(Build({1124,-7716,957}, {0,1,0}, eye, left, {0,0,1}, 50, g));
		BOOST_REQUIRE(Build({1124,-7716,957}, {0,1,0}, eye, left, {0,0,1}, 50, again));
		BOOST_CHECK(g.points == again.points);
		BOOST_CHECK_SMALL(std::sqrt(Dot(Sub(g.points[0],g.points[1]),
			Sub(g.points[0],g.points[1]))) - 2*g.radius, 0.002f);
	}
}
BOOST_AUTO_TEST_CASE(reject_nonfinite_and_degenerate_view)
{
	Geometry g;
	BOOST_CHECK(!Build({0,0,0}, {1,0,0}, {3,0,0}, {0,1,0}, {0,0,1}, 50, g));
	BOOST_CHECK(!Build({0,0,0}, {1,0,0}, {100,0,0}, {}, {0,0,1}, 50, g));
	BOOST_CHECK(!Build({0,0,0}, {1,0,0}, {100,0,0}, {0,1,0}, {0,1,0}, 50, g));
	BOOST_CHECK(!Build({std::numeric_limits<float>::infinity(),0,0}, {1,0,0},
		{100,0,0}, {0,1,0}, {0,0,1}, 50, g));
}
BOOST_AUTO_TEST_SUITE_END()
