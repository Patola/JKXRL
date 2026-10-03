#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_deform.h"

BOOST_AUTO_TEST_SUITE(material_deformation)
BOOST_AUTO_TEST_CASE(coincident_flat_cutouts_select_one_authored_side)
{
	for (float distance : {0.01f, 1.0f, 8.0f, 128.0f})
	{
		BOOST_CHECK(VK_DeformFaceVisible(distance, false, 0, true));
		BOOST_CHECK(!VK_DeformFaceVisible(-distance, false, 0, true));
		BOOST_CHECK(!VK_DeformFaceVisible(distance, true, 0, true));
		BOOST_CHECK(VK_DeformFaceVisible(-distance, true, 0, true));
	}
}
BOOST_AUTO_TEST_CASE(planar_liquid_sides_and_near_plane_margin)
{
	// Artus liquid brushes carry a lit top and a black-lightmapped underside.
	// Both winding directions must not composite when viewed from above.
	for (float eyeHeight : {24.0f, 64.0f, 500.0f})
	{
		BOOST_CHECK(VK_DeformFaceVisible(eyeHeight, false, 3));
		BOOST_CHECK(!VK_DeformFaceVisible(-eyeHeight, false, 3));
		BOOST_CHECK(!VK_DeformFaceVisible(eyeHeight, true, 3));
		BOOST_CHECK(VK_DeformFaceVisible(-eyeHeight, true, 3));
	}
	for (float distance : {-11.0f, -1.0f, 0.0f, 1.0f, 11.0f})
	{
		BOOST_CHECK(VK_DeformFaceVisible(distance, false, 3));
		BOOST_CHECK(VK_DeformFaceVisible(distance, true, 3));
	}
	BOOST_CHECK(VK_DeformFaceVisible(-40, false, 64));
	BOOST_CHECK(!VK_DeformFaceVisible(-40, false, 0));
}
BOOST_AUTO_TEST_CASE(identity_and_uniform_bulge)
{
	const std::array<float, 3> p{2, 3, 4}, n{0, 0, 1};
	BOOST_CHECK(VK_DeformPosition({}, p, n, 0.3f) == p);
	vk_deform_t d;
	d.type = VK_DEFORM_BULGE;
	d.vector = {0, 12, 0};
	auto result = VK_DeformPosition(VK_DeformBlock({d}, 50, 20), p, n, 0.3f);
	BOOST_CHECK_EQUAL(result[2], 16);
	d.vector[1] = -0.2f;
	result = VK_DeformPosition(VK_DeformBlock({d}, 50, 20), p, n, 0.3f);
	BOOST_CHECK_CLOSE(result[2], 3.8f, 0.001f);
}
BOOST_AUTO_TEST_CASE(wave_zero_frequency_ignores_spread)
{
	vk_deform_t d;
	d.spread = 100;
	d.wave = {1, 2, 0.25f, 0};
	const auto block = VK_DeformBlock({d}, 10, 2);
	const auto a = VK_DeformPosition(block, {0,0,0}, {0,0,1}, 0);
	const auto b = VK_DeformPosition(block, {0.123f,5,0}, {0,0,1}, 0);
	BOOST_CHECK_EQUAL(a[2], b[2]);
	BOOST_CHECK_CLOSE(a[2], 3, 0.001f);
}
BOOST_AUTO_TEST_CASE(authored_order_clocks_and_bounds)
{
	vk_deform_t move, wave, bulge;
	move.type = VK_DEFORM_MOVE;
	move.vector = {3,-2,1};
	move.wave = {1,0,0,0};
	wave.spread = 0.1f;
	wave.wave = {0,2,0,0.5f};
	bulge.type = VK_DEFORM_BULGE;
	bulge.vector = {4,3,1};
	const std::array<float, 3> p{0.1f,0.2f,0.3f}, n{0,0,1};
	const auto ordered = VK_DeformPosition(VK_DeformBlock({move,wave,bulge}, 3, 1), p,n,0.2f);
	auto sequential = p;
	for (const auto& d : {move,wave,bulge})
		sequential = VK_DeformPosition(VK_DeformBlock({d},3,1), sequential,n,0.2f);
	BOOST_CHECK(ordered == sequential);
	const auto reversed = VK_DeformPosition(VK_DeformBlock({wave,move,bulge},3,1),p,n,0.2f);
	BOOST_CHECK(ordered != reversed);
	BOOST_CHECK(VK_DeformPosition(VK_DeformBlock({bulge},3,0),p,n,0.2f) ==
		VK_DeformPosition(VK_DeformBlock({bulge},3,2),p,n,0.2f));
	BOOST_CHECK_EQUAL(VK_DeformExtent(move), 3);
	BOOST_CHECK_EQUAL(VK_DeformExtent(wave), 2);
	BOOST_CHECK_EQUAL(VK_DeformExtent(bulge), 3);
	BOOST_CHECK_EQUAL(VK_DeformBlock({move,wave,bulge,move},1,0).control[0], 3);
}
BOOST_AUTO_TEST_CASE(lookup_table_negative_phase)
{
	for (int function = VK_WAVE_SIN; function <= VK_WAVE_INVERSE_SAWTOOTH; ++function)
		for (int i = -4096; i < 4096; ++i)
		{
			float phase = i / 1733.0f;
			const int index = static_cast<int>(phase * 1024) & 1023;
			const auto type = static_cast<vk_waveform_t>(function);
			const float expected = type == VK_WAVE_SIN
				? std::sin(index * (6.28318530717958647692f / 1023.0f))
				: VK_EvaluateWaveform(type, index / 1024.0f);
			BOOST_CHECK_SMALL(VK_DeformWave(type, phase) - expected, 1e-6f);
		}
}
BOOST_AUTO_TEST_SUITE_END()
