/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_material_blend.h"
#include "../code/rd-vulkan/vk_deform.h"

#include <algorithm>
#include <array>

namespace
{
using Color = std::array<float, 4>;

float Factor( VkBlendFactor factor, const Color &src, const Color &dst, int c )
{
	switch ( factor )
	{
	case VK_BLEND_FACTOR_ZERO: return 0.0f;
	case VK_BLEND_FACTOR_ONE: return 1.0f;
	case VK_BLEND_FACTOR_SRC_ALPHA: return src[3];
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return 1.0f - src[3];
	case VK_BLEND_FACTOR_DST_ALPHA: return dst[3];
	case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return 1.0f - dst[3];
	case VK_BLEND_FACTOR_SRC_COLOR: return src[c];
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return 1.0f - src[c];
	case VK_BLEND_FACTOR_DST_COLOR: return dst[c];
	default: BOOST_FAIL( "Unexpected blend factor" ); return 0.0f;
	}
}

Color Composite( vk_blend_mode_t mode, const Color &src, const Color &dst )
{
	const auto state = VK_MaterialBlendAttachment( mode );
	if ( !state.blendEnable ) return src;
	Color result = {};
	for ( int c = 0; c < 4; ++c )
	{
		result[c] = std::min( 1.0f,
			src[c] * Factor( c == 3 ? state.srcAlphaBlendFactor : state.srcColorBlendFactor, src, dst, c ) +
			dst[c] * Factor( c == 3 ? state.dstAlphaBlendFactor : state.dstColorBlendFactor, src, dst, c ) );
	}
	return result;
}
}

BOOST_AUTO_TEST_SUITE( MaterialBlend )

BOOST_AUTO_TEST_CASE( InverseAlphaPreservesAuthoredEnergyMask )
{
	const Color energy{0.2f,0.5f,0.9f,1};
	for (float alpha : {0.f,0.25f,0.75f,1.f})
	{
		const Color frame{0.6f,0.3f,0.1f,alpha};
		const auto inverse = Composite(VK_BLEND_INVERSE_ALPHA, frame, energy);
		const auto both = Composite(VK_BLEND_INVERSE_ALPHA_BOTH, frame, energy);
		for (int c=0; c<4; ++c)
		{
			BOOST_CHECK_SMALL(inverse[c] - (frame[c]*(1-alpha)+energy[c]*alpha), 0.000001f);
			BOOST_CHECK_SMALL(both[c] - std::min(1.f,(frame[c]+energy[c])*(1-alpha)), 0.000001f);
		}
		if (alpha != 0.5f) BOOST_CHECK(inverse != Composite(VK_BLEND_ALPHA, frame, energy));
	}
}

BOOST_AUTO_TEST_CASE(OverlappingImplicitSurfacesMustFinishTheirOwnLightmapPass)
{
	const Color albedo{0.5f,0.4f,0.3f,1}, light{0.44f,0.44f,0.44f,1};
	const auto single = Composite(VK_BLEND_MODULATE, light, albedo);
	const auto grouped = Composite(VK_BLEND_MODULATE, light, single);
	const auto separate = Composite(VK_BLEND_MODULATE, light,
		Composite(VK_BLEND_OPAQUE, albedo, single));
	BOOST_CHECK(separate == single);
	BOOST_CHECK_LT(grouped[0], single[0]);
}

BOOST_AUTO_TEST_CASE( DeformingLiquidUndersideMustNotEraseTop )
{
	const Color black{0, 0, 0, 1}, green{0.05f, 0.35f, 0.01f, 1};
	const Color litTop{0.4f, 0.4f, 0.4f, 1};
	const auto surface = [&](Color dst, const Color &lightmap) {
		dst = Composite(VK_BLEND_ADDITIVE, green, dst);
		dst = Composite(VK_BLEND_ADDITIVE, green, dst);
		return Composite(VK_BLEND_DOUBLE_MODULATE, lightmap, dst);
	};
	const auto top = surface(black, litTop);
	const auto broken = surface(top, black);
	BOOST_CHECK_GT(top[1], 0.5f);
	BOOST_CHECK_EQUAL(broken[1], 0.0f);
	Color selected = black;
	if (VK_DeformFaceVisible(100, false, 3)) selected = surface(selected, litTop);
	if (VK_DeformFaceVisible(-100, false, 3)) selected = surface(selected, black);
	BOOST_CHECK(selected == top);
}

BOOST_AUTO_TEST_CASE( ExistingPipelineFactorsRemainUnchanged )
{
	struct Expected
	{
		vk_blend_mode_t mode;
		VkBlendFactor srcRGB, dstRGB, srcAlpha, dstAlpha;
	};
	const Expected expected[] = {
		{ VK_BLEND_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
		{ VK_BLEND_OPAQUE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
		{ VK_BLEND_ADDITIVE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE },
		{ VK_BLEND_SOURCE_ALPHA_ADDITIVE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE },
		{ VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE },
		{ VK_BLEND_ONE_SOURCE_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA },
		{ VK_BLEND_DESTINATION_COLOR_ADDITIVE, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE },
		{ VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE },
		{ VK_BLEND_MODULATE, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO,
			VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ZERO },
		{ VK_BLEND_DOUBLE_MODULATE, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_SRC_COLOR,
			VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA },
		{ VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
			VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
		{ VK_BLEND_SCREEN, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
		{ VK_BLEND_ONE_SOURCE_COLOR, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA },
		{ VK_BLEND_INVERSE_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA },
		{ VK_BLEND_INVERSE_ALPHA_BOTH, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
	};
	for ( const auto &e : expected )
	{
		const auto state = VK_MaterialBlendAttachment( e.mode );
		BOOST_CHECK_EQUAL( state.blendEnable, e.mode != VK_BLEND_OPAQUE );
		BOOST_CHECK_EQUAL( state.srcColorBlendFactor, e.srcRGB );
		BOOST_CHECK_EQUAL( state.dstColorBlendFactor, e.dstRGB );
		BOOST_CHECK_EQUAL( state.srcAlphaBlendFactor, e.srcAlpha );
		BOOST_CHECK_EQUAL( state.dstAlphaBlendFactor, e.dstAlpha );
		BOOST_CHECK_EQUAL( state.colorBlendOp, VK_BLEND_OP_ADD );
		BOOST_CHECK_EQUAL( state.alphaBlendOp, VK_BLEND_OP_ADD );
		BOOST_CHECK_EQUAL( state.colorWriteMask, 15u );
	}
}

BOOST_AUTO_TEST_CASE( OpaqueImageDoesNotEraseHologramBackground )
{
	// Wedge's JPEG energy layers have alpha=1; their RGB, not alpha,
	// determines how much of the previous stages and room remains visible.
	const Color tint = { 1.0f / 255.0f, 69.0f / 255.0f, 168.0f / 255.0f, 1.0f };
	const Color energy = { 0.2f, 0.25f, 0.4f, 1.0f };
	const Color roomDark = { 0.1f, 0.2f, 0.3f, 1.0f };
	const Color roomLight = { 0.6f, 0.7f, 0.8f, 1.0f };
	const auto stages = [&]( const Color &room, vk_blend_mode_t finalBlend )
	{
		return Composite( finalBlend, energy,
			Composite( VK_BLEND_ADDITIVE, energy, Composite( VK_BLEND_MODULATE, tint, room ) ) );
	};
	const Color dark = stages( roomDark, VK_BLEND_ONE_SOURCE_COLOR );
	const Color light = stages( roomLight, VK_BLEND_ONE_SOURCE_COLOR );
	for ( int c = 0; c < 3; ++c )
	{
		BOOST_CHECK_GT( light[c], dark[c] );
		BOOST_CHECK_SMALL( dark[c] - ( energy[c] + ( roomDark[c] * tint[c] + energy[c] ) * energy[c] ), 0.000001f );
		BOOST_CHECK_EQUAL( stages( roomDark, VK_BLEND_ALPHA )[c], energy[c] );
		BOOST_CHECK_EQUAL( stages( roomLight, VK_BLEND_ALPHA )[c], energy[c] );
	}
	BOOST_CHECK_EQUAL( dark[3], 1.0f );
	const Color partialAlpha = Composite( VK_BLEND_ONE_SOURCE_COLOR,
		Color{0.2f, 0.3f, 0.4f, 0.2f}, Color{0.1f, 0.2f, 0.3f, 0.6f} );
	BOOST_CHECK_SMALL( partialAlpha[3] - 0.32f, 0.000001f );
}

BOOST_AUTO_TEST_CASE( ModelCompositePhasesAreDisjoint )
{
	for ( bool fullyBlended : { false, true } )
	{
		BOOST_CHECK( VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_ALL, fullyBlended ) );
		const bool early = VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_SOLID, fullyBlended );
		const bool late = VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_BLENDED, fullyBlended );
		BOOST_CHECK_NE( early, late );
		BOOST_CHECK_EQUAL( late, fullyBlended );
	}
}

BOOST_AUTO_TEST_CASE( AuthoredCullIsRestrictedToLateMD3s )
{
	for ( const auto cull : { VK_MATERIAL_FRONT_SIDED, VK_MATERIAL_BACK_SIDED,
		VK_MATERIAL_TWO_SIDED } )
	{
		BOOST_CHECK_EQUAL( VK_BlendedMD3CullMode( cull, false ), VK_CULL_MODE_NONE );
	}
	BOOST_CHECK_EQUAL( VK_BlendedMD3CullMode( VK_MATERIAL_FRONT_SIDED, true ), VK_CULL_MODE_FRONT_BIT );
	BOOST_CHECK_EQUAL( VK_BlendedMD3CullMode( VK_MATERIAL_BACK_SIDED, true ), VK_CULL_MODE_BACK_BIT );
	BOOST_CHECK_EQUAL( VK_BlendedMD3CullMode( VK_MATERIAL_TWO_SIDED, true ), VK_CULL_MODE_NONE );

	// Vulkan's signed framebuffer area is minus the usual shoelace area.
	// The world projection negates Y, so CCW classifications match legacy GL.
	const auto area = []( const std::array<std::array<float, 2>, 3> &points )
	{
		float sum = 0.0f;
		for ( int i = 0; i < 3; ++i )
		{
			const auto &a = points[i];
			const auto &b = points[( i + 1 ) % 3];
			sum += a[0] * b[1] - b[0] * a[1];
		}
		return sum * 0.5f;
	};
	for ( float winding : { -1.0f, 1.0f } )
	{
		std::array<std::array<float, 2>, 3> triangle =
			{{{ -winding, -1 }, { 0, 1 }, { winding, -1 }}};
		const float glArea = area( triangle );
		for ( auto &vertex : triangle ) vertex[1] = -vertex[1];
		BOOST_CHECK_EQUAL( glArea, -area( triangle ) );
		BOOST_CHECK_NE( glArea, 0.0f );
	}
}

BOOST_AUTO_TEST_CASE( DepthAlphaCullDoesNotChangeOrdinaryModels )
{
	BOOST_CHECK( VK_DepthAlphaGLMCull( true, true, VK_BLEND_ALPHA, true, false ) );
	BOOST_CHECK( !VK_DepthAlphaGLMCull( false, true, VK_BLEND_ALPHA, true, false ) );
	BOOST_CHECK( !VK_DepthAlphaGLMCull( true, false, VK_BLEND_ALPHA, true, false ) );
	BOOST_CHECK( !VK_DepthAlphaGLMCull( true, true, VK_BLEND_ALPHA, false, false ) );
	BOOST_CHECK( !VK_DepthAlphaGLMCull( true, true, VK_BLEND_ALPHA, true, true ) );
	BOOST_CHECK( !VK_DepthAlphaGLMCull( true, true, VK_BLEND_ADDITIVE, true, false ) );
}

BOOST_AUTO_TEST_CASE( TranslucentShellHidesCoveredJointsWithoutHidingScenery )
{
	const Color scenery = { 0.2f, 0.3f, 0.1f, 1.0f };
	const Color sleeve = { 0.4f, 0.6f, 0.9f, 0.7f };
	const Color wrist = { 0.9f, 0.1f, 0.1f, 0.7f };
	const Color glow = { 0.02f, 0.03f, 0.05f, 1.0f };
	const Color expected = Composite( VK_BLEND_ADDITIVE, glow,
		Composite( VK_BLEND_ALPHA, sleeve, scenery ) );
	// Both faces point toward the camera: face culling alone cannot hide the wrist.
	for ( bool wristFirst : { false, true } )
	{
		const std::array<float, 2> depths = wristFirst
			? std::array<float, 2>{ 0.6f, 0.4f } : std::array<float, 2>{ 0.4f, 0.6f };
		const std::array<Color, 2> colors = wristFirst
			? std::array<Color, 2>{ wrist, sleeve } : std::array<Color, 2>{ sleeve, wrist };
		float depth = 1.0f;
		Color pixel = scenery;
		for ( float candidate : depths ) depth = std::min( depth, candidate );
		BOOST_CHECK( pixel == scenery ); // Depth-only must not touch color.
		for ( size_t i = 0; i < depths.size(); ++i )
		{
			if ( depths[i] <= depth )
			{
				pixel = Composite( VK_BLEND_ALPHA, colors[i], pixel );
				pixel = Composite( VK_BLEND_ADDITIVE, glow, pixel );
			}
		}
		BOOST_CHECK( pixel == expected );
		BOOST_CHECK( pixel != sleeve ); // Still translucent against the scenery.
	}
	const Color contaminated = Composite( VK_BLEND_ADDITIVE, glow,
		Composite( VK_BLEND_ALPHA, sleeve,
			Composite( VK_BLEND_ADDITIVE, glow, Composite( VK_BLEND_ALPHA, wrist, scenery ) ) ) );
	BOOST_CHECK( contaminated != expected ); // Old rear-first compositing failure.
}

BOOST_AUTO_TEST_CASE( ExtraBackFacesChangePulsingContourContrast )
{
	const Color room = { 0.3f, 0.3f, 0.3f, 1.0f };
	const Color tint = { 1.0f / 255.0f, 69.0f / 255.0f, 168.0f / 255.0f, 1.0f };
	const auto projection = [&]( int layers, const Color &energy )
	{
		Color color = room;
		for ( int i = 0; i < layers; ++i ) color = Composite( VK_BLEND_MODULATE, tint, color );
		for ( int i = 0; i < layers; ++i ) color = Composite( VK_BLEND_ADDITIVE, energy, color );
		for ( int i = 0; i < layers; ++i ) color = Composite( VK_BLEND_ONE_SOURCE_COLOR, energy, color );
		return color;
	};
	const Color dimEnergy = { 0.2f, 0.25f, 0.4f, 1.0f };
	// At this pulse phase the authored one/two-layer intersection brightens.
	// Doubling coverage reverses that contrast without changing a blend factor.
	BOOST_CHECK_GT( projection( 2, dimEnergy )[2], projection( 1, dimEnergy )[2] );
	BOOST_CHECK_LT( projection( 4, dimEnergy )[2], projection( 2, dimEnergy )[2] );
	const Color brightEnergy = { 0.3f, 0.35f, 0.65f, 1.0f };
	BOOST_CHECK_GT( projection( 1, brightEnergy )[2], projection( 1, dimEnergy )[2] );
}

BOOST_AUTO_TEST_CASE( BlendedModelCompositesAfterRoomFinishingLayers )
{
	const Color lightmap = { 0.5f, 0.6f, 0.7f, 1.0f };
	const Color wallTexture = { 0.15f, 0.2f, 0.25f, 1.0f };
	const Color tint = { 1.0f / 255.0f, 69.0f / 255.0f, 168.0f / 255.0f, 1.0f };
	const Color energy = { 0.2f, 0.25f, 0.4f, 1.0f };
	const auto hologram = [&]( const Color &room )
	{
		return Composite( VK_BLEND_ONE_SOURCE_COLOR, energy,
			Composite( VK_BLEND_ADDITIVE, energy, Composite( VK_BLEND_MODULATE, tint, room ) ) );
	};
	Color actual = lightmap;
	int draws = 0;
	if ( VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_SOLID, true ) )
	{
		actual = hologram( actual );
		++draws;
	}
	actual = Composite( VK_BLEND_MODULATE, wallTexture, actual );
	if ( VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_BLENDED, true ) )
	{
		actual = hologram( actual );
		++draws;
	}
	const Color expected = hologram( Composite( VK_BLEND_MODULATE, wallTexture, lightmap ) );
	const Color oldOrder = Composite( VK_BLEND_MODULATE, wallTexture, hologram( lightmap ) );
	BOOST_CHECK_EQUAL( draws, 1 );
	for ( int c = 0; c < 3; ++c )
	{
		BOOST_CHECK_SMALL( actual[c] - expected[c], 0.000001f );
		BOOST_CHECK_GT( actual[c], oldOrder[c] );
	}
}

BOOST_AUTO_TEST_CASE( MixedCrystalModelSeparatesSurfaceContracts )
{
	// The pillar has an opaque environment stage plus alpha albedo. Its crystal
	// surfaces use additive environment mapping without any depth-writing stage.
	const bool stoneLate = VK_ModelSurfaceIsLate( true, false, true );
	const bool crystalLate = VK_ModelSurfaceIsLate( false, false, true );
	BOOST_CHECK( !stoneLate && crystalLate );
	BOOST_CHECK( !VK_ModelSurfaceIsLate( false, true, true ) );
	BOOST_CHECK( !VK_ModelSurfaceIsLate( false, false, false ) );
	for ( bool late : { stoneLate, crystalLate } )
	{
		const int draws = VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_SOLID, late ) +
			VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_BLENDED, late );
		BOOST_CHECK_EQUAL( draws, 1 );
	}
	const Color background = { 0.8f, 0.8f, 0.8f, 1.0f };
	const Color wall = { 0.1f, 0.15f, 0.2f, 1.0f };
	const Color crystal = { 0.4f, 0.3f, 0.6f, 1.0f };
	Color actual = background;
	if ( VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_SOLID, crystalLate ) )
		actual = Composite( VK_BLEND_ADDITIVE, crystal, actual );
	actual = Composite( VK_BLEND_MODULATE, wall, actual );
	if ( VK_ModelCompositePhaseIncludes( VK_MODEL_COMPOSITE_BLENDED, crystalLate ) )
		actual = Composite( VK_BLEND_ADDITIVE, crystal, actual );
	const Color expected = Composite( VK_BLEND_ADDITIVE, crystal,
		Composite( VK_BLEND_MODULATE, wall, background ) );
	const Color oldOrder = Composite( VK_BLEND_MODULATE, wall,
		Composite( VK_BLEND_ADDITIVE, crystal, background ) );
	for ( int c = 0; c < 3; ++c )
	{
		BOOST_CHECK_SMALL( actual[c] - expected[c], 0.000001f );
		BOOST_CHECK_GT( actual[c], oldOrder[c] );
	}
}

BOOST_AUTO_TEST_SUITE_END()
