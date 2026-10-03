/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <boost/test/unit_test.hpp>
#include "../JKXR/VrJumpInput.h"

BOOST_AUTO_TEST_SUITE( VrJumpInput )

BOOST_AUTO_TEST_CASE( GameplayHoldAndRelease )
{
    jkxr_jump_input_t state;
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 1);
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 0);
    BOOST_CHECK_EQUAL(state.Update(false, true, false), -1);
    BOOST_CHECK_EQUAL(state.Update(false, true, false), 0);
}

BOOST_AUTO_TEST_CASE( MenuConsumesReleaseWithoutLeavingJumpStuck )
{
    jkxr_jump_input_t state;
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 1);
    BOOST_CHECK_EQUAL(state.Update(false, false, false), -1);
    BOOST_CHECK_EQUAL(state.Update(false, true, false), 0);
    BOOST_CHECK(!state.held);
}

BOOST_AUTO_TEST_CASE( MenuPressCannotBecomeGameplayJump )
{
    jkxr_jump_input_t state;
    BOOST_CHECK_EQUAL(state.Update(true, false, false), 0);
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 0);
    BOOST_CHECK_EQUAL(state.Update(false, true, false), 0);
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 1);
}

BOOST_AUTO_TEST_CASE( CameraExitConsumesHoldUntilFreshPress )
{
    jkxr_jump_input_t state;
    BOOST_CHECK_EQUAL(state.Update(false, true, true), 0);
    BOOST_CHECK_EQUAL(state.Update(true, true, true), 1);
    BOOST_CHECK_EQUAL(state.Update(true, true, false), -1);
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 0);
    BOOST_CHECK_EQUAL(state.Update(false, true, false), 0);
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 1);
}

BOOST_AUTO_TEST_CASE( EnterCameraWithButtonHeldDoesNotImmediatelyExit )
{
    jkxr_jump_input_t state;
    BOOST_CHECK_EQUAL(state.Update(true, true, false), 1);
    BOOST_CHECK_EQUAL(state.Update(true, true, true), -1);
    BOOST_CHECK_EQUAL(state.Update(true, true, true), 0);
    BOOST_CHECK_EQUAL(state.Update(false, true, true), 0);
    BOOST_CHECK_EQUAL(state.Update(true, true, true), 1);
}

BOOST_AUTO_TEST_SUITE_END()
