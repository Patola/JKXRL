#include "../code/rd-vulkan/vk_md3_animation.h"
#include "md3_upload_reference.h"

#include <limits>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( md3_animation )

BOOST_AUTO_TEST_CASE( frame_selection_matches_legacy_contract )
{
	auto blend = VK_MD3FrameBlend( 5, 4, 3, 0.25f, false, false );
	BOOST_CHECK_EQUAL( blend.frame, 4 );
	BOOST_CHECK_EQUAL( blend.oldFrame, 3 );
	BOOST_CHECK_EQUAL( blend.backlerp, 0.25f );
	blend = VK_MD3FrameBlend( 5, 8, 6, 0.5f, true, true );
	BOOST_CHECK_EQUAL( blend.frame, 4 );
	BOOST_CHECK_EQUAL( blend.oldFrame, 4 );
	BOOST_CHECK_EQUAL( blend.backlerp, 0.0f );
	blend = VK_MD3FrameBlend( 5, 8, 6, 0.5f, false, true );
	BOOST_CHECK_EQUAL( blend.frame, 3 );
	BOOST_CHECK_EQUAL( blend.oldFrame, 1 );
	for ( int invalid : { -1, 5, 999 } )
	{
		blend = VK_MD3FrameBlend( 5, 4, invalid, 0.5f, false, false );
		BOOST_CHECK_EQUAL( blend.frame, 0 );
		BOOST_CHECK_EQUAL( blend.oldFrame, 0 );
		BOOST_CHECK_EQUAL( blend.backlerp, 0.0f );
	}
	BOOST_CHECK_EQUAL( VK_MD3FrameBlend( 0, 4, 3, 0.5f, false, true ).frame, 0 );
	BOOST_CHECK_EQUAL( VK_MD3FrameBlend( 5, 4, 3,
		std::numeric_limits<float>::quiet_NaN(), false, false ).backlerp, 0.0f );
}

BOOST_AUTO_TEST_CASE( interpolates_position_and_normal_not_texture_coordinates )
{
	const vk_md3_pose_vertex_t current = { { 8, 4, 2 }, { 1, 0, 0 } };
	const vk_md3_pose_vertex_t previous = { { 0, 0, 0 }, { 0, 1, 0 } };
	const auto start = VK_MD3InterpolateVertex( current, previous, 1.0f );
	const auto middle = VK_MD3InterpolateVertex( current, previous, 0.5f );
	const auto end = VK_MD3InterpolateVertex( current, previous, 0.0f );
	BOOST_CHECK_EQUAL( start.position[0], 0.0f );
	BOOST_CHECK_EQUAL( middle.position[0], 4.0f );
	BOOST_CHECK_EQUAL( middle.position[1], 2.0f );
	BOOST_CHECK_EQUAL( middle.position[2], 1.0f );
	BOOST_CHECK_EQUAL( end.position[0], 8.0f );
	BOOST_CHECK_CLOSE( middle.normal[0], std::sqrt( 0.5f ), 0.001f );
	BOOST_CHECK_CLOSE( middle.normal[1], std::sqrt( 0.5f ), 0.001f );
	const vk_md3_pose_vertex_t opposite = { { 0, 0, 0 }, { -1, 0, 0 } };
	const auto degenerate = VK_MD3InterpolateVertex( current, opposite, 0.5f );
	BOOST_CHECK_EQUAL( degenerate.normal[0], 1.0f );
}

BOOST_AUTO_TEST_CASE( lever_sequence_advances_and_keeps_final_pose )
{
	for ( int frame = 1; frame <= 4; ++frame )
	{
		const vk_md3_pose_vertex_t current = { { float( frame ), 0, 0 }, { 0, 0, 1 } };
		const vk_md3_pose_vertex_t previous = { { float( frame - 1 ), 0, 0 }, { 0, 0, 1 } };
		float last = float( frame - 1 );
		for ( int step = 0; step <= 10; ++step )
		{
			const auto blend = VK_MD3FrameBlend( 5, frame, frame - 1,
				1.0f - step / 10.0f, true, false );
			const auto pose = VK_MD3InterpolateVertex( current, previous, blend.backlerp );
			BOOST_CHECK_GE( pose.position[0], last );
			last = pose.position[0];
		}
		BOOST_CHECK_EQUAL( last, float( frame ) );
	}
	BOOST_CHECK_EQUAL( VK_MD3FrameBlend( 5, 4, 4, 0.5f, true, false ).frame, 4 );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_CASE( md3_cpu_preparation_is_byte_identical_to_in_place_stream )
{
	for ( int seed = 0; seed < 256; ++seed )
	for ( bool animated : { false, true } )
	for ( bool lit : { false, true } )
	for ( float backlerp : { 0.0f, 0.125f, 0.5f, 1.0f } )
	{
		const Md3TestVertex base = { { float( seed ), -1, 2 }, { 0.2f, 0.3f, 0.4f, 0.5f },
			{ 0.25f, 0.75f }, { 0.1f, 0.2f }, { 0.6f, -0.8f, 0 } };
		const vk_md3_pose_vertex_t current = { { float( seed ), 2, 3 }, { 1, 0, 0 } };
		const vk_md3_pose_vertex_t previous = { { -2, 6, 0 }, { -1, 0, 0 } };
		const float ambient[3] = { float( seed ), 20, 190 };
		const float directed[3] = { 100, float( seed ), 190 };
		const float direction[3] = { seed % 2 ? -0.8f : 0.8f, 0.6f, 0 };
		Md3TestVertex reference = base;
		Md3Reference( reference, animated ? &current : nullptr, animated ? &previous : nullptr,
			backlerp, lit ? ambient : nullptr, directed, direction );
		const auto actual = VK_MD3PrepareVertex( base,
			animated ? &current : nullptr, animated ? &previous : nullptr,
			backlerp, lit ? ambient : nullptr, directed, direction );
		BOOST_CHECK_EQUAL( std::memcmp( &actual, &reference, sizeof( actual ) ), 0 );
	}
}
