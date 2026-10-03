#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_billboard.h"
#include "../code/rd-vulkan/vk_deform.h"

using namespace vk_billboard;
static void Close(Point a, Point b)
{
	for (int i = 0; i < 3; ++i) BOOST_CHECK_SMALL(a[i] - b[i], 0.002f);
}
static Quad Chain()
{
	return {{{{12400,-8,-2200}, {12400,8,-2200}, {12400,8,-2000}, {12400,-8,-2000}}},
		{{0,1,3,3,1,2}}, 2};
}
BOOST_AUTO_TEST_SUITE(material_billboards)
BOOST_AUTO_TEST_CASE(stock_korriban_order_and_long_chains)
{
	for (const Quad& q : {
		Quad{{{{4,-32,-768},{4,-32,-800},{-4,-32,-800},{-4,-32,-768}}},{{3,0,2,2,0,1}},2},
		Quad{{{{13822,2030,-2240},{13822,2030,-2724},{13822,2034,-2724},{13822,2034,-2240}}},{{3,0,2,2,0,1}},2}})
	{
		for (int degrees = 0; degrees < 360; degrees += 5)
		{
			const float a = degrees * 0.0174532925f;
			Matrix m{};
			BOOST_REQUIRE(Frame(q,{std::cos(a),std::sin(a),0},{-std::sin(a),std::cos(a),0},{0,0,1},m));
			Close(Scale(Add(Transform(m,q.points[0]),Transform(m,q.points[3])),0.5f),
				Scale(Add(q.points[0],q.points[3]),0.5f));
			Close(Scale(Add(Transform(m,q.points[1]),Transform(m,q.points[2])),0.5f),
				Scale(Add(q.points[1],q.points[2]),0.5f));
		}
	}
}
BOOST_AUTO_TEST_CASE(chain_preserves_anchors_width_and_stereo_geometry)
{
	const auto q = Chain();
	for (int angle = 0; angle < 360; ++angle)
	{
		float a = angle * 0.0174532925f;
		Point forward{std::cos(a), std::sin(a), 0}, left{-std::sin(a), std::cos(a), 0};
		Matrix m{}, secondEye{};
		BOOST_REQUIRE(Frame(q, forward, left, {0,0,1}, m));
		// Eye translation is deliberately not an input to the shared surface.
		BOOST_REQUIRE(Frame(q, forward, left, {0,0,1}, secondEye));
		BOOST_CHECK(m == secondEye);
		const auto p0 = Transform(m,q.points[0]), p1 = Transform(m,q.points[1]);
		const auto p2 = Transform(m,q.points[2]), p3 = Transform(m,q.points[3]);
		Close(Scale(Add(p0,p1),0.5f), {12400,0,-2200});
		Close(Scale(Add(p2,p3),0.5f), {12400,0,-2000});
		BOOST_CHECK_SMALL(std::sqrt(Dot(Sub(p0,p1),Sub(p0,p1))) - 16, 0.002f);
		BOOST_CHECK_SMALL(Dot(Sub(p0,p1),forward), 0.002f);
		vk_deform_block_t block{};
		block.billboard = m;
		block.control[3] = 1;
		Close(VK_DeformPosition(block,q.points[0],{1,0,0},0), p0);
	}
}
BOOST_AUTO_TEST_CASE(square_center_radius_and_normal)
{
	Quad q{{{{0,-8,8},{0,8,8},{0,8,-8},{0,-8,-8}}},{{0,1,3,3,1,2}},1};
	Matrix m{};
	BOOST_REQUIRE(Frame(q,{0,1,0},{-1,0,0},{0,0,1},m));
	Point center{};
	for (const auto& p : q.points) center = Add(center, Transform(m,p));
	Close(center,{0,0,0});
	Close(Transform(m,q.points[0]),{-7.998792f,0,7.998792f});
	Close({m[3][0],m[3][1],m[3][2]}, {0,-1,0});
}
BOOST_AUTO_TEST_CASE(parallel_view_stays_finite_and_malformed_quads_are_rejected)
{
	auto q = Chain(); Matrix m{};
	BOOST_REQUIRE(Frame(q,{0,0,1},{0,1,0},{1,0,0},m));
	for (auto p:q.points) for (float x:Transform(m,p)) BOOST_CHECK(std::isfinite(x));
	q.points[3][0] += 5;
	BOOST_CHECK(!Frame(q,{1,0,0},{0,1,0},{0,0,1},m));
	q = Chain(); q.indices[0] = 4;
	BOOST_CHECK(!Frame(q,{1,0,0},{0,1,0},{0,0,1},m));
	q = Chain(); q.points[1] = q.points[0];
	BOOST_CHECK(!Frame(q,{1,0,0},{0,1,0},{0,0,1},m));
}
BOOST_AUTO_TEST_SUITE_END()
