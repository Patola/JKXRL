/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include <utility>
#include "../JKXR/VrMountedAim.h"

BOOST_AUTO_TEST_SUITE(VrMountedAim)

BOOST_AUTO_TEST_CASE(DeadzoneAndFineControl)
{
    BOOST_CHECK_EQUAL(jkxr_mounted_stick_t::Response(0.1f), 0.0f);
    BOOST_CHECK_EQUAL(jkxr_mounted_stick_t::Response(1.0f), 1.0f);
    BOOST_CHECK_EQUAL(jkxr_mounted_stick_t::Response(-1.0f), -1.0f);
    BOOST_CHECK(jkxr_mounted_stick_t::Response(0.5f) < 0.25f);
    BOOST_CHECK(jkxr_mounted_stick_t::Response(0.5f) > 0.0f);
}

BOOST_AUTO_TEST_CASE(EntryAndMenuRequireCenteredStick)
{
    jkxr_mounted_stick_t s;
    float yaw, pitch;
    s.Update(1, 1, 1000, true, 90, 60, yaw, pitch);
    s.Update(1, 1, 1010, true, 90, 60, yaw, pitch);
    BOOST_CHECK_EQUAL(yaw, 0);
    s.Update(0, 0, 1020, true, 90, 60, yaw, pitch);
    s.Update(1, 1, 1030, true, 90, 60, yaw, pitch);
    BOOST_CHECK_CLOSE(yaw, -0.9f, 0.001f);
    BOOST_CHECK_CLOSE(pitch, -0.6f, 0.001f);
    s.Update(1, 1, 1040, false, 90, 60, yaw, pitch);
    s.Update(1, 1, 1050, true, 90, 60, yaw, pitch);
    BOOST_CHECK_EQUAL(yaw, 0);
    BOOST_CHECK_EQUAL(pitch, 0);
}

BOOST_AUTO_TEST_CASE(SameRateAtDifferentFrameRates)
{
    for (int step : {10, 20})
    {
        jkxr_mounted_stick_t s;
        float yaw, pitch, totalYaw = 0, totalPitch = 0;
        s.Update(0, 0, 1000, true, 90, 60, yaw, pitch);
        for (int time = 1000 + step; time <= 2000; time += step)
        {
            s.Update(1, -1, time, true, 90, 60, yaw, pitch);
            totalYaw += yaw;
            totalPitch += pitch;
        }
        BOOST_CHECK_CLOSE(totalYaw, -90.0f, 0.001f);
        BOOST_CHECK_CLOSE(totalPitch, 60.0f, 0.001f);
    }
}

BOOST_AUTO_TEST_CASE(HitchDoesNotCreateAimJump)
{
    jkxr_mounted_stick_t s;
    float yaw, pitch;
    s.Update(0, 0, 1000, true, 90, 60, yaw, pitch);
    s.Update(1, 1, 2000, true, 90, 60, yaw, pitch);
    BOOST_CHECK_EQUAL(yaw, 0);
    BOOST_CHECK_EQUAL(pitch, 0);
    s.Update(-1, -1, 2010, true, 90, 60, yaw, pitch);
    BOOST_CHECK_CLOSE(yaw, 0.9f, 0.001f);
    BOOST_CHECK_CLOSE(pitch, 0.6f, 0.001f);
}

BOOST_AUTO_TEST_CASE(PitchCannotWindUpAtEitherStop)
{
    for (const auto limits : {std::pair<float, float>{-15, 10}, {-35, 30}})
    {
        float pitch = 0;
        for (int i = 0; i < 10000; ++i)
            pitch = JKXR_AdvanceMountedPitch(pitch, 0.66f, limits.first, limits.second);
        BOOST_CHECK_EQUAL(pitch, limits.second);
        pitch = JKXR_AdvanceMountedPitch(pitch, -0.66f, limits.first, limits.second);
        BOOST_CHECK_CLOSE(pitch, limits.second - 0.66f, 0.001f);
        for (int i = 0; i < 10000; ++i)
            pitch = JKXR_AdvanceMountedPitch(pitch, -0.66f, limits.first, limits.second);
        BOOST_CHECK_EQUAL(pitch, limits.first);
        pitch = JKXR_AdvanceMountedPitch(pitch, 0.66f, limits.first, limits.second);
        BOOST_CHECK_CLOSE(pitch, limits.first + 0.66f, 0.001f);
    }
}

BOOST_AUTO_TEST_CASE(ChangedPitchLimitsRecoverBeforeApplyingInput)
{
    BOOST_CHECK_CLOSE(JKXR_AdvanceMountedPitch(30, -1, -15, 10), 9.0f, 0.001f);
    BOOST_CHECK_CLOSE(JKXR_AdvanceMountedPitch(-35, 1, -15, 10), -14.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(MountingConsumesHeldUseUntilReleaseAndNewPress)
{
    jkxr_mounted_exit_t exit;
    BOOST_CHECK(!exit.Update(45, 1000, 1010, true, false));
    BOOST_CHECK(!exit.Update(45, 1000, 1600, true, false));
    BOOST_CHECK(!exit.Update(45, 1000, 6000, true, false));
    BOOST_CHECK(!exit.Update(45, 1000, 6010, false, false));
    BOOST_CHECK(exit.Update(45, 1000, 6020, true, false));
}

BOOST_AUTO_TEST_CASE(EntryJumpIsConsumedButFreshJumpCanDismount)
{
    jkxr_mounted_exit_t exit;
    BOOST_CHECK(!exit.Update(45, 1000, 1010, true, true));
    BOOST_CHECK(!exit.Update(45, 1000, 1600, true, true));
    BOOST_CHECK(!exit.Update(45, 1000, 1700, true, false));
    BOOST_CHECK(exit.Update(45, 1000, 1800, true, true));
}

BOOST_AUTO_TEST_CASE(PressDuringMountDelayDoesNotBecomeDelayedExit)
{
    jkxr_mounted_exit_t exit;
    BOOST_CHECK(!exit.Update(45, 1000, 1010, false, false));
    BOOST_CHECK(!exit.Update(45, 1000, 1200, true, false));
    BOOST_CHECK(!exit.Update(45, 1000, 1600, true, false));
    BOOST_CHECK(!exit.Update(45, 1000, 1700, false, false));
    BOOST_CHECK(exit.Update(45, 1000, 1800, true, false));
    BOOST_CHECK(!exit.Update(45, 2000, 2600, true, false));
    BOOST_CHECK(!exit.Update(12, 3000, 3600, true, false));
    BOOST_CHECK(!exit.Update(12, 100, 700, true, false));
}

BOOST_AUTO_TEST_SUITE_END()
