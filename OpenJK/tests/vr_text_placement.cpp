/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include <limits>
#include "../JKXR/VrTextPlacement.h"

BOOST_AUTO_TEST_SUITE(VrTextPlacement)

BOOST_AUTO_TEST_CASE(CenteredAfterHudOriginTransform)
{
    for (float scale : {0.25f, 0.4f, 0.5f, 1.0f})
        for (float width : {0.0f, 31.0f, 120.0f, 250.0f})
            for (float eyeOffset : {-20.0f, 0.0f, 20.0f})
            {
                const float x = VR_CenteredTextOrigin(320, width, scale);
                const float drawCenter = x * scale + 320 * (1 - scale) + eyeOffset + width / 2;
                BOOST_CHECK_SMALL(drawCenter - (320 + eyeOffset), 0.501f);
            }
}

BOOST_AUTO_TEST_CASE(UnscaledPanelsAndInvalidScale)
{
    BOOST_CHECK_EQUAL(VR_CenteredTextOrigin(320, 200, 1), 220);
    BOOST_CHECK_EQUAL(VR_CenteredTextOrigin(320, 200, 0), 220);
    BOOST_CHECK_EQUAL(VR_CenteredTextOrigin(320, 200, -1), 220);
    BOOST_CHECK_EQUAL(VR_CenteredTextOrigin(320, 200, std::numeric_limits<float>::quiet_NaN()), 220);
}

BOOST_AUTO_TEST_SUITE_END()
