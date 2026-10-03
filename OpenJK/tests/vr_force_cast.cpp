/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../JKXR/VrForceCast.h"
#include "../JKXR/VrSelectorTime.h"

BOOST_AUTO_TEST_SUITE(VrForceCast)

BOOST_AUTO_TEST_CASE(TriggerImmediatelyHoldsSelectedPower)
{
    jkxr_force_cast_t s;
    const auto e = s.Update(true, true, false);
    BOOST_CHECK(e.ownsTrigger && !e.armed);
    BOOST_CHECK(s.SelectedPowerHeld() && !s.CanGesture());
    for (int i = 0; i < 1000; ++i)
    {
        s.Update(true, true, false);
        BOOST_CHECK(s.SelectedPowerHeld() && !s.CanGesture());
    }
    s.Update(false, true, false);
    BOOST_CHECK(!s.SelectedPowerHeld() && !s.CanGesture());
}

BOOST_AUTO_TEST_CASE(GripThenTriggerChoosesOneGesture)
{
    jkxr_force_cast_t s;
    s.Update(false, true, true);
    BOOST_CHECK(s.Update(true, true, true).armed);
    BOOST_CHECK(s.CanGesture() && !s.SelectedPowerHeld());
    s.Consume();
    BOOST_CHECK(!s.CanGesture() && !s.SelectedPowerHeld());
    s.Update(false, true, true);
    BOOST_CHECK(!s.SelectedPowerHeld());
    BOOST_CHECK(s.Update(true, true, true).armed);
}

BOOST_AUTO_TEST_CASE(ChangingGripDuringHoldCannotChangeMode)
{
    jkxr_force_cast_t s;
    s.Update(true, true, false);
    s.Update(true, true, true);
    BOOST_CHECK(s.SelectedPowerHeld() && !s.CanGesture());
    s.Update(false, true, true);
    s.Update(true, true, true);
    s.Update(true, true, false);
    BOOST_CHECK(!s.SelectedPowerHeld() && s.CanGesture());
}

BOOST_AUTO_TEST_CASE(CancelledHoldRequiresRelease)
{
    for (bool grip : {false, true})
    {
        jkxr_force_cast_t s;
        s.Update(true, true, grip);
        BOOST_CHECK(s.Update(true, false, grip).ownsTrigger);
        s.Update(true, true, grip);
        BOOST_CHECK(!s.CanGesture() && !s.SelectedPowerHeld());
        s.Update(false, true, grip);
        s.Update(true, true, grip);
        BOOST_CHECK(s.active);
    }
}

BOOST_AUTO_TEST_CASE(HeldOnEntryDoesNotActivate)
{
    jkxr_force_cast_t s;
    s.Update(true, false, false);
    s.Update(true, true, true);
    BOOST_CHECK(!s.CanGesture() && !s.SelectedPowerHeld());
    s.Update(false, true, true);
    BOOST_CHECK(s.Update(true, true, true).armed);
}

BOOST_AUTO_TEST_CASE(SelectedPowerAfterConsumedGesture)
{
    for (bool releaseGripFirst : {false, true})
    {
        jkxr_force_cast_t s;
        s.Update(true, true, true);
        s.Consume();
        if (releaseGripFirst) s.Update(true, true, false);
        s.Update(false, true, !releaseGripFirst);
        s.Update(false, true, false);
        s.Update(true, true, false);
        BOOST_CHECK(s.SelectedPowerHeld());
        BOOST_CHECK(!s.CanGesture());
        s.Update(false, true, false);
        BOOST_CHECK(!s.owned && !s.active);
    }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(VrSelectorTime)

BOOST_AUTO_TEST_CASE(GripWheelToGestureRestoresTime)
{
    jkxr_selector_time_t wheel;
    jkxr_force_cast_t cast;
    float time = 1.0f;
    BOOST_REQUIRE(wheel.Begin(time));
    time = wheel.SlowScale;
    for (int i = 0; i < 120; ++i)
        BOOST_CHECK(!wheel.Begin(time));
    time = wheel.End(time); // Cancel the wheel before arming the chord.
    BOOST_CHECK_EQUAL(time, 1.0f);
    BOOST_CHECK(cast.Update(true, true, true).armed);
    cast.Consume();
    BOOST_CHECK_EQUAL(wheel.End(time), 1.0f);
}

BOOST_AUTO_TEST_CASE(RestoresPriorTimeRatherThanAssumingNormalSpeed)
{
    jkxr_selector_time_t wheel;
    for (float prior : {0.5f, 1.0f, 1.5f})
    {
        BOOST_CHECK(wheel.Begin(prior));
        BOOST_CHECK_EQUAL(wheel.End(wheel.SlowScale), prior);
        BOOST_CHECK(!wheel.active);
    }
}

BOOST_AUTO_TEST_CASE(DoesNotOverwriteAnExternalTimeChange)
{
    jkxr_selector_time_t wheel;
    wheel.Begin(1.0f);
    BOOST_CHECK(!wheel.Begin(0.5f));
    BOOST_CHECK_EQUAL(wheel.End(0.5f), 0.5f);
    BOOST_CHECK_EQUAL(wheel.End(0.22f), 0.22f);
}

BOOST_AUTO_TEST_CASE(CancelBeforeFirstDrawIsHarmless)
{
    jkxr_selector_time_t wheel;
    BOOST_CHECK_EQUAL(wheel.End(1.0f), 1.0f);
    BOOST_CHECK(wheel.Begin(1.0f));
    BOOST_CHECK_EQUAL(wheel.End(wheel.SlowScale), 1.0f);
}

BOOST_AUTO_TEST_SUITE_END()
