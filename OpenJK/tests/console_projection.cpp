/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_console_projection.h"
#include <limits>

BOOST_AUTO_TEST_SUITE( ConsoleProjection )

BOOST_AUTO_TEST_CASE( FrontViewKeepsAcceptedSizeAndOrientation )
{
	float clip[3];
	BOOST_REQUIRE( VK_ConsoleViewClipPoint( 1, 2, -6, -1, 1, -1, 1, clip ) );
	BOOST_CHECK_EQUAL( clip[0], 1 );
	BOOST_CHECK_EQUAL( clip[1], -2 );
	BOOST_CHECK_EQUAL( clip[2], 6 );
}

BOOST_AUTO_TEST_CASE( AsymmetricFovMatchesGameProjection )
{
	const float l = -0.9f, r = 1.1f, d = -1.2f, u = 0.8f;
	float matrix[16] = {};
	matrix[0] = 2 / ( r - l );
	matrix[5] = -2 / ( u - d );
	matrix[8] = ( r + l ) / ( r - l );
	matrix[9] = -( u + d ) / ( u - d );
	matrix[11] = -1;
	for ( float z : { -6.0f, -0.001f, 0.0f, 0.001f, 6.0f } )
	{
		const float point[3] = { 2, 3, z };
		float game[3], xr[3];
		BOOST_REQUIRE( VK_ConsoleGameClipPoint( matrix, point, game ) );
		BOOST_REQUIRE( VK_ConsoleViewClipPoint( 2, 3, z, l, r, d, u, xr ) );
		for ( int i = 0; i < 3; ++i ) BOOST_CHECK_SMALL( game[i] - xr[i], 0.000001f );
	}
}

BOOST_AUTO_TEST_CASE( LookingPastPanelRetainsSignedWWithoutFallback )
{
	for ( int degrees = 0; degrees <= 360; ++degrees )
	{
		const double yaw = degrees * 3.141592653589793 / 180;
		for ( float edge : { -3.0f, 3.0f } )
		{
			const float x = edge * cos( yaw ) - 6 * sin( yaw );
			const float z = -edge * sin( yaw ) - 6 * cos( yaw );
			float clip[3];
			BOOST_REQUIRE( VK_ConsoleViewClipPoint( x, 0, z, -1, 1, -1, 1, clip ) );
			BOOST_CHECK_EQUAL( clip[2], -z );
		}
	}
	float left[3], right[3];
	BOOST_REQUIRE( VK_ConsoleViewClipPoint( -6, 0, 3, -1, 1, -1, 1, left ) );
	BOOST_REQUIRE( VK_ConsoleViewClipPoint( -6, 0, -3, -1, 1, -1, 1, right ) );
	BOOST_CHECK_LT( left[2], 0 );
	BOOST_CHECK_GT( right[2], 0 );
}

BOOST_AUTO_TEST_CASE( PreservePerspectiveForGlyphInterpolation )
{
	float a[3], b[3];
	VK_ConsoleViewClipPoint( -1, 0, -2, -1, 1, -1, 1, a );
	VK_ConsoleViewClipPoint( 1, 0, -6, -1, 1, -1, 1, b );
	const float uv = ( 0.5f / b[2] ) / ( 0.5f / a[2] + 0.5f / b[2] );
	BOOST_CHECK_SMALL( uv - 0.25f, 0.000001f );
	float middle[3];
	VK_ConsoleViewClipPoint( -1 + uv * 2, 0, -2 - uv * 4, -1, 1, -1, 1, middle );
	BOOST_CHECK_SMALL( middle[0] / middle[2] - ( a[0] / a[2] + b[0] / b[2] ) / 2, 0.000001f );
}

BOOST_AUTO_TEST_CASE( InvalidDataIsRejectedButEyePlaneIsNot )
{
	float clip[3];
	BOOST_CHECK( VK_ConsoleViewClipPoint( 2, 1, 0, -1, 1, -1, 1, clip ) );
	BOOST_CHECK( !VK_ConsoleViewClipPoint( 2, 1, -6, 1, 1, -1, 1, clip ) );
	BOOST_CHECK( !VK_ConsoleViewClipPoint( std::numeric_limits<float>::infinity(), 1, -6, -1, 1, -1, 1, clip ) );
}

BOOST_AUTO_TEST_SUITE_END()
