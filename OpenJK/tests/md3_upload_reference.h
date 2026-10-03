/* Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later. */
#pragma once
#include "../code/rd-vulkan/vk_md3_animation.h"
#include <cstring>

struct Md3TestVertex
{
	float position[3], color[4], uv[2], lightmapUv[2], normal[3];
};
static_assert( sizeof( Md3TestVertex ) == 56, "Match streamed world vertex layout" );

// Original in-place algorithm, retained solely as an output/benchmark oracle.
inline void Md3Reference( Md3TestVertex &output, const vk_md3_pose_vertex_t *current,
	const vk_md3_pose_vertex_t *previous, float backlerp,
	const float *ambient, const float *directed, const float *direction )
{
	if ( current )
	{
		const auto pose = VK_MD3InterpolateVertex( *current, *previous, backlerp );
		std::memcpy( output.position, pose.position, sizeof( pose.position ) );
		std::memcpy( output.normal, pose.normal, sizeof( pose.normal ) );
	}
	if ( ambient )
	{
		const float incoming = std::max( 0.0f, output.normal[0] * direction[0] +
			output.normal[1] * direction[1] + output.normal[2] * direction[2] );
		for ( int channel = 0; channel < 3; ++channel )
			output.color[channel] = std::min( 255.0f, ambient[channel] + incoming * directed[channel] ) / 255.0f;
		output.color[3] = 1.0f;
	}
}
