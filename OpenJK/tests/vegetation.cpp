#include "../code/rd-vulkan/vk_surface_sprites.h"

#include <limits>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( vegetation )

BOOST_AUTO_TEST_CASE( bounded_distance_setting )
{
	BOOST_CHECK_EQUAL( VK_VegetationDistanceScale( -10.0f ), 1.0f );
	BOOST_CHECK_EQUAL( VK_VegetationDistanceScale( 3.0f ), 3.0f );
	BOOST_CHECK_EQUAL( VK_VegetationDistanceScale( 100.0f ), 4.0f );
	BOOST_CHECK_EQUAL( VK_VegetationDistanceScale( std::numeric_limits<float>::infinity() ), 3.0f );
	BOOST_CHECK_EQUAL( VK_VegetationDistanceScale( std::numeric_limits<float>::quiet_NaN() ), 3.0f );
}

BOOST_AUTO_TEST_CASE( continuous_monotonic_fade_for_every_phase )
{
	for ( int phaseIndex = 0; phaseIndex < 100; ++phaseIndex )
	{
		const float phase = phaseIndex * 0.063f;
		for ( float scale : { 1.0f, 3.0f, 4.0f } )
		{
			const float start = 500.0f * scale;
			const float end = 1500.0f * scale;
			BOOST_CHECK_EQUAL( VK_VegetationCoverage( start, start, end, phase ), 1.0f );
			BOOST_CHECK_EQUAL( VK_VegetationCoverage( end, start, end, phase ), 0.0f );
			float previous = 1.0f;
			for ( int step = 0; step <= 1000; ++step )
			{
				const float distance = start + ( end - start ) * step / 1000.0f;
				const float alpha = VK_VegetationCoverage( distance, start, end, phase );
				BOOST_CHECK_GE( alpha, 0.0f );
				BOOST_CHECK_LE( alpha, previous + 0.000001f );
				BOOST_CHECK_LT( previous - alpha, 0.003f );
				previous = alpha;
			}
		}
	}
}

BOOST_AUTO_TEST_CASE( distance_scaling_preserves_fade_shape )
{
	for ( int distance = 0; distance <= 1800; distance += 10 )
	{
		const float near = VK_VegetationCoverage( distance, 500.0f, 1500.0f, 0.4f );
		const float far = VK_VegetationCoverage( distance * 3.0f, 1500.0f, 4500.0f, 0.4f );
		BOOST_CHECK_SMALL( near - far, 0.000001f );
	}
}

BOOST_AUTO_TEST_SUITE_END()
