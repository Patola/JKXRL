/* Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later. */
#ifndef VK_MD3_ANIMATION_H
#define VK_MD3_ANIMATION_H

#include <algorithm>
#include <cmath>

struct vk_md3_pose_vertex_t
{
	float position[3];
	float normal[3];
};

struct vk_md3_frame_blend_t
{
	int frame;
	int oldFrame;
	float backlerp;
};

inline vk_md3_frame_blend_t VK_MD3FrameBlend(
	int frameCount, int frame, int oldFrame, float backlerp, bool cap, bool wrap )
{
	if ( frameCount <= 0 )
	{
		return {};
	}
	// Match R_AddMD3Surfaces: capping takes precedence over wrapping, and
	// an invalid frame resets the pair rather than indexing outside the mesh.
	if ( cap )
	{
		frame = std::min( frame, frameCount - 1 );
		oldFrame = std::min( oldFrame, frameCount - 1 );
	}
	else if ( wrap )
	{
		frame %= frameCount;
		oldFrame %= frameCount;
	}
	if ( frame < 0 || oldFrame < 0 || frame >= frameCount || oldFrame >= frameCount )
	{
		return {};
	}
	return { frame, oldFrame, frame == oldFrame || !std::isfinite( backlerp )
		? 0.0f : std::clamp( backlerp, 0.0f, 1.0f ) };
}

inline vk_md3_pose_vertex_t VK_MD3InterpolateVertex(
	const vk_md3_pose_vertex_t &current, const vk_md3_pose_vertex_t &previous,
	float backlerp )
{
	vk_md3_pose_vertex_t result;
	float lengthSquared = 0.0f;
	for ( int axis = 0; axis < 3; ++axis )
	{
		result.position[axis] = current.position[axis] * ( 1.0f - backlerp ) +
			previous.position[axis] * backlerp;
		result.normal[axis] = current.normal[axis] * ( 1.0f - backlerp ) +
			previous.normal[axis] * backlerp;
		lengthSquared += result.normal[axis] * result.normal[axis];
	}
	for ( int axis = 0; axis < 3; ++axis )
	{
		result.normal[axis] = lengthSquared > 0.000001f
			? result.normal[axis] / std::sqrt( lengthSquared ) : current.normal[axis];
	}
	return result;
}

// Work on a CPU-local vertex, never read back from a mapped upload buffer.
// Vertex keeps the renderer's full layout (including both sets of UVs).
template<class Vertex>
inline Vertex VK_MD3PrepareVertex( const Vertex &base,
	const vk_md3_pose_vertex_t *current, const vk_md3_pose_vertex_t *previous,
	float backlerp, const float *ambient, const float *directed, const float *localDirection )
{
	Vertex result = base;
	if ( current != nullptr && previous != nullptr )
	{
		const auto pose = VK_MD3InterpolateVertex( *current, *previous, backlerp );
		for ( int axis = 0; axis < 3; ++axis )
		{
			result.position[axis] = pose.position[axis];
			result.normal[axis] = pose.normal[axis];
		}
	}
	if ( ambient != nullptr )
	{
		const float incoming = std::max( 0.0f,
			result.normal[0] * localDirection[0] + result.normal[1] * localDirection[1] +
			result.normal[2] * localDirection[2] );
		for ( int channel = 0; channel < 3; ++channel )
		{
			result.color[channel] = std::min( 255.0f,
				ambient[channel] + incoming * directed[channel] ) / 255.0f;
		}
		result.color[3] = 1.0f;
	}
	return result;
}

#endif
