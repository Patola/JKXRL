/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_security_camera.h"
#include "../JKXR/VrCameraVisibility.h"
#include <limits>

BOOST_AUTO_TEST_SUITE( SecurityCamera )

BOOST_AUTO_TEST_CASE( OnlyActiveCameraAndItsSavedBaseAreHidden )
{
	const float camera[3] = {100, 200, 300}, base[3] = {100, 200, 316};
	const float neighbor[3] = {132, 200, 316};
	const char* model = "models/map_objects/kejim/impcam_base.md3";
	BOOST_CHECK(VR_HideActiveCameraPart(true, true, "", camera, camera));
	BOOST_CHECK(VR_HideActiveCameraPart(true, false, model, base, camera));
	BOOST_CHECK(!VR_HideActiveCameraPart(false, true, model, base, camera));
	BOOST_CHECK(!VR_HideActiveCameraPart(true, false, model, neighbor, camera));
	BOOST_CHECK(!VR_HideActiveCameraPart(true, false, "other.md3", base, camera));
}

BOOST_AUTO_TEST_CASE( MonitorProjectionIsFourByThree )
{
	for ( float degrees : { 10.0f, 60.0f, 90.0f, 120.0f, 150.0f } )
	{
		const auto fov = VK_SecurityCameraFov( degrees );
		BOOST_CHECK_CLOSE( std::tan( fov.horizontalHalfAngle ) /
			std::tan( fov.verticalHalfAngle ), 4.0f / 3.0f, 0.001f );
		BOOST_CHECK( fov.horizontalHalfAngle > fov.verticalHalfAngle );
	}
}

BOOST_AUTO_TEST_CASE( InvalidFovCannotProduceBrokenMonitor )
{
	const auto fallback = VK_SecurityCameraFov( 90.0f );
	for ( float degrees : { 0.0f, -10.0f,
		std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() } )
	{
		const auto fov = VK_SecurityCameraFov( degrees );
		BOOST_CHECK_EQUAL( fov.horizontalHalfAngle, fallback.horizontalHalfAngle );
		BOOST_CHECK_EQUAL( fov.verticalHalfAngle, fallback.verticalHalfAngle );
	}
	BOOST_CHECK_EQUAL( VK_SecurityCameraFov( 180.0f ).horizontalHalfAngle,
		VK_SecurityCameraFov( 150.0f ).horizontalHalfAngle );
}

BOOST_AUTO_TEST_SUITE_END()
