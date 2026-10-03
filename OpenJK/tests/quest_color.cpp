/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_quest_color.h"
#include "../code/rd-vulkan/vk_quest_color_matrix.h"

#include <vector>

BOOST_AUTO_TEST_SUITE( QuestColor )

BOOST_AUTO_TEST_CASE( ExplicitOptInAndFormatGuard )
{
	for ( int requested = -8; requested <= 8; ++requested )
	{
		BOOST_CHECK_EQUAL( VK_QuestColorProfile( requested, false ), 0 );
		BOOST_CHECK_EQUAL( VK_QuestColorProfile( requested, true ),
			requested >= 0 && requested <= 3 ? requested : 0 );
	}
}

BOOST_AUTO_TEST_CASE( LegacyUploadLookup )
{
	const auto gamma = VK_QuestTextureLookup( false );
	const auto mip = VK_QuestTextureLookup( true );
	BOOST_CHECK_EQUAL( gamma[0], 0 );
	BOOST_CHECK_EQUAL( gamma[255], 255 );
	BOOST_CHECK_EQUAL( mip[0], 0 );
	BOOST_CHECK_EQUAL( mip[255], 255 );
	// Independent scalar version of GLES R_SetColorMappings/R_LightScaleTexture.
	for ( int i = 0; i < 256; ++i )
	{
		int intensified = i * 1.07f;
		if ( intensified > 255 ) intensified = 255;
		const int expected = 255 * pow( intensified / 255.0, 1.0 / 1.15f ) + 0.5;
		BOOST_CHECK_EQUAL( mip[i], expected );
		BOOST_CHECK_GE( mip[i], gamma[i] );
		if ( i != 0 ) BOOST_CHECK_GE( mip[i], mip[i - 1] );
	}
}

BOOST_AUTO_TEST_CASE( PicmipBeforeGammaAndLowerMipsAfterGamma )
{
	// Raw chain: 4x4 checker -> 2x2 gray -> 1x1 gray. Values intentionally
	// straddle the nonlinear lookup, so reducing corrected L0 would fail.
	std::vector<uint8_t> raw( ( 16 + 4 + 1 ) * 4 );
	for ( size_t i = 0; i < 21; ++i )
	{
		const uint8_t color = i < 16 ? ( i % 2 ? 200 : 0 ) : 100;
		for ( int c = 0; c < 3; ++c ) raw[i * 4 + c] = color;
		raw[i * 4 + 3] = static_cast<uint8_t>( 17 + i );
	}
	auto prepared = raw;
	VK_ApplyQuestTextureProfile( prepared.data(), 4, 4, 3, 1, true );
	const auto lookup = VK_QuestTextureLookup( true );
	BOOST_CHECK_EQUAL( prepared[0], lookup[0] );
	BOOST_CHECK_EQUAL( prepared[4], lookup[200] );
	BOOST_CHECK_EQUAL( prepared[64], lookup[100] );
	BOOST_CHECK_EQUAL( prepared[80], lookup[100] );
	BOOST_CHECK_NE( prepared[64], ( lookup[0] + lookup[200] ) / 2 );
	for ( size_t i = 0; i < 21; ++i ) BOOST_CHECK_EQUAL( prepared[i * 4 + 3], raw[i * 4 + 3] );

	prepared = raw;
	VK_ApplyQuestTextureProfile( prepared.data(), 4, 4, 3, 0, true );
	BOOST_CHECK_EQUAL( prepared[64], ( lookup[0] + lookup[200] ) / 2 );
	BOOST_CHECK_EQUAL( prepared[80], prepared[64] );
}

BOOST_AUTO_TEST_CASE( OnePixelAndNarrowImages )
{
	uint8_t pixel[] = { 1, 69, 168, 42 };
	VK_ApplyQuestTextureProfile( pixel, 1, 1, 1, 99, true );
	const auto lookup = VK_QuestTextureLookup( true );
	BOOST_CHECK_EQUAL( pixel[1], lookup[69] );
	BOOST_CHECK_EQUAL( pixel[2], lookup[168] );
	BOOST_CHECK_EQUAL( pixel[3], 42 );
	uint8_t narrow[] = { 0, 0, 0, 9, 200, 200, 200, 11, 100, 100, 100, 10 };
	VK_ApplyQuestTextureProfile( narrow, 1, 2, 2, -1, true );
	BOOST_CHECK_EQUAL( narrow[8], ( lookup[0] + lookup[200] ) / 2 );
	BOOST_CHECK_EQUAL( narrow[11], 10 );
}

BOOST_AUTO_TEST_CASE( GamutMatrixPreservesNeutralAxis )
{
	const double matrix[3][3] = {
		{ VK_QUEST_COLOR_ROW_R }, { VK_QUEST_COLOR_ROW_G }, { VK_QUEST_COLOR_ROW_B }
	};
	for ( int i = 0; i <= 255; ++i )
	{
		const double encoded = i / 255.0;
		const double linear = encoded <= 0.04045 ? encoded / 12.92 : pow( ( encoded + 0.055 ) / 1.055, 2.4 );
		for ( const auto &row : matrix )
		{
			const double converted = std::clamp( linear * ( row[0] + row[1] + row[2] ), 0.0, 1.0 );
			const double result = converted <= 0.0031308 ? converted * 12.92 : 1.055 * pow( converted, 1.0 / 2.4 ) - 0.055;
			BOOST_CHECK_SMALL( result - encoded, 0.000001 );
		}
	}
	// Linear blue/green mix becomes less red, not more violet. Catch a
	// transposed matrix (GLSL mat3 constructors are column-major).
	const double linearColor[3] = { 0.1, 0.3, 0.5 };
	double out[3] = {};
	for ( int row = 0; row < 3; ++row )
		for ( int c = 0; c < 3; ++c ) out[row] += matrix[row][c] * linearColor[c];
	BOOST_CHECK_SMALL( out[0] - ( -0.0466682 ), 0.000001 );
	BOOST_CHECK_SMALL( out[1] - 0.3232405, 0.000001 );
	BOOST_CHECK_SMALL( out[2] - 0.5273762, 0.000001 );
}

BOOST_AUTO_TEST_SUITE_END()
