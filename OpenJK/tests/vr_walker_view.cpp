/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <boost/test/unit_test.hpp>
#include "../JKXR/VrWalkerView.h"

BOOST_AUTO_TEST_SUITE(VrWalkerView)

BOOST_AUTO_TEST_CASE(ElevatedBehindHull)
{
    float offset[3];
    JKXR_WalkerCameraOffset(0, 0, 0, 248, offset);
    BOOST_CHECK_CLOSE(offset[0], -240.0f, 0.001f);
    BOOST_CHECK_SMALL(offset[1], 0.001f);
    BOOST_CHECK_EQUAL(offset[2], 280.0f);
}

BOOST_AUTO_TEST_CASE(LookingAroundDoesNotOrbitCamera)
{
    float initial[3], turned[3];
    JKXR_WalkerCameraOffset(35, 15, 15, 248, initial);
    JKXR_WalkerCameraOffset(95, 75, 15, 248, turned);
    for (int axis = 0; axis < 3; ++axis)
        BOOST_CHECK_SMALL(initial[axis] - turned[axis], 0.001f);
}

BOOST_AUTO_TEST_CASE(StickTurnRotatesChasePosition)
{
    float offset[3];
    JKXR_WalkerCameraOffset(90, 0, 0, 248, offset);
    BOOST_CHECK_SMALL(offset[0], 0.001f);
    BOOST_CHECK_CLOSE(offset[1], -240.0f, 0.001f);
    BOOST_CHECK_EQUAL(offset[2], 280.0f);
}

BOOST_AUTO_TEST_CASE(YawWrapIsContinuous)
{
    float before[3], after[3];
    JKXR_WalkerCameraOffset(181, 179, 0, 248, before);
    JKXR_WalkerCameraOffset(-179, 179, 0, 248, after);
    for (int axis = 0; axis < 3; ++axis)
        BOOST_CHECK_SMALL(before[axis] - after[axis], 0.001f);
}

BOOST_AUTO_TEST_SUITE_END()
