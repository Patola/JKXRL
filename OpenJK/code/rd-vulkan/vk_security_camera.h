/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef JKXR_VK_SECURITY_CAMERA_H
#define JKXR_VK_SECURITY_CAMERA_H

#include <algorithm>
#include <cmath>

struct vk_security_camera_fov_t
{
	float horizontalHalfAngle;
	float verticalHalfAngle;
};

inline vk_security_camera_fov_t VK_SecurityCameraFov( float horizontalDegrees )
{
	if ( !std::isfinite( horizontalDegrees ) || horizontalDegrees <= 0.0f )
	{
		horizontalDegrees = 90.0f;
	}
	const float half = std::clamp( horizontalDegrees, 10.0f, 150.0f ) *
		0.008726646259971648f;
	// The shared virtual monitor is 4:3, irrespective of the eye image extent.
	return { half, std::atan( std::tan( half ) * 0.75f ) };
}

#endif
