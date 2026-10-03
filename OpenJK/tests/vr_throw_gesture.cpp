/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include <limits>
#include "../JKXR/VrThrowGesture.h"

BOOST_AUTO_TEST_SUITE(VrThrowGesture)

using Gesture = jkxr_throw_gesture_t;
using Release = Gesture::Release;

static bool Sample(Gesture &g, Release &r, int time, float z, bool held = true,
                   bool allowed = true, int grace = 250)
{
    return g.Update(time, {0, 1.3f, z}, {0, 0, z}, held, allowed, grace, r);
}

static void Stroke(Gesture &g, Release &r)
{
    for (int i = 0; i <= 10; ++i) Sample(g, r, 1000 + i*10, -0.3f - i*0.02f);
}

BOOST_AUTO_TEST_CASE(ReleaseDuringThrowAtDifferentFrameRates)
{
    for (int step : {7, 11, 16, 22, 33, 50})
    {
        Gesture g;
        Release r;
        for (int i = 0; i < 12; ++i)
            BOOST_CHECK(!Sample(g, r, 1000+i*step, -0.3f - i*step*0.002f));
        BOOST_REQUIRE(Sample(g, r, 1000+12*step, -0.3f-12*step*0.002f, false));
        BOOST_CHECK_CLOSE(r.velocity[2], -2.0f, 0.01f);
    }
}

BOOST_AUTO_TEST_CASE(LateReleaseRetainsCompletedThrow)
{
    Gesture g;
    Release r;
    Stroke(g, r);
    for (int t = 1110; t < 1290; t += 10) Sample(g, r, t, -0.5f);
    BOOST_REQUIRE(Sample(g, r, 1290, -0.5f, false));
    BOOST_CHECK(r.assisted);
    BOOST_CHECK_CLOSE(r.velocity[2], -2.0f, 0.01f);
    BOOST_CHECK_SMALL(r.currentSpeed, 0.001f);
    BOOST_CHECK(r.strokeAgeMs <= 250);
    BOOST_CHECK(!Sample(g, r, 1300, -0.5f, false));
}

BOOST_AUTO_TEST_CASE(FastRecoveryDoesNotThrowBackAtPlayer)
{
    Gesture g;
    Release r;
    Stroke(g, r);
    for (int i = 1; i < 6; ++i) Sample(g, r, 1100+i*10, -0.5f+i*0.035f);
    BOOST_REQUIRE(Sample(g, r, 1160, -0.29f, false));
    BOOST_CHECK(r.assisted);
    BOOST_CHECK(r.currentSpeed > 3.0f);
    BOOST_CHECK_CLOSE(r.velocity[2], -2.0f, 0.01f);
}

BOOST_AUTO_TEST_CASE(ExpiredStrokeAllowsDeliberateDrop)
{
    Gesture g;
    Release r;
    Stroke(g, r);
    for (int t = 1110; t < 1600; t += 10) Sample(g, r, t, -0.5f);
    BOOST_REQUIRE(Sample(g, r, 1600, -0.5f, false));
    BOOST_CHECK(!r.assisted);
    BOOST_CHECK_SMALL(Gesture::Length(r.velocity), 0.001f);
}

BOOST_AUTO_TEST_CASE(GentleTossAndStationaryDropRemainProportional)
{
    for (float speed : {0.0f, 0.2f, 0.5f})
    {
        Gesture g;
        Release r;
        for (int i = 0; i < 10; ++i) Sample(g, r, 1000+i*10, -0.3f-i*0.01f*speed);
        BOOST_REQUIRE(Sample(g, r, 1100, -0.3f-0.1f*speed, false));
        BOOST_CHECK(!r.assisted);
        BOOST_CHECK_SMALL(r.velocity[2]+speed, 0.001f);
    }
}

BOOST_AUTO_TEST_CASE(DisabledGraceDoesNotRecoverPastStroke)
{
    Gesture g;
    Release r;
    Stroke(g, r);
    for (int t = 1110; t < 1200; t += 10) Sample(g, r, t, -0.5f, true, true, 0);
    BOOST_REQUIRE(Sample(g, r, 1200, -0.5f, false, true, 0));
    BOOST_CHECK(!r.assisted);
    BOOST_CHECK_SMALL(Gesture::Length(r.velocity), 0.001f);
}

BOOST_AUTO_TEST_CASE(NewChargeDoesNotReusePreviousThrow)
{
    Gesture g;
    Release r;
    Stroke(g, r);
    BOOST_REQUIRE(Sample(g, r, 1110, -0.52f, false));
    for (int t = 1120; t < 1200; t += 10) Sample(g, r, t, -0.52f);
    BOOST_REQUIRE(Sample(g, r, 1200, -0.52f, false));
    BOOST_CHECK_SMALL(Gesture::Length(r.velocity), 0.001f);
}

BOOST_AUTO_TEST_CASE(RecentWeakerStrokeSurvivesAnOlderPeakExpiring)
{
    Gesture g;
    Release r;
    Stroke(g, r);
    for (int t = 1110; t <= 1250; t += 10) Sample(g, r, t, -0.5f);
    for (int i = 1; i <= 10; ++i) Sample(g, r, 1250+i*10, -0.5f-i*0.01f);
    for (int t = 1360; t < 1500; t += 10) Sample(g, r, t, -0.6f);
    BOOST_REQUIRE(Sample(g, r, 1500, -0.6f, false));
    BOOST_CHECK(r.assisted);
    BOOST_CHECK_CLOSE(r.velocity[2], -1.0f, 0.01f);
}

BOOST_AUTO_TEST_CASE(DisabledContextRequiresFreshPress)
{
    Gesture g;
    Release r;
    Stroke(g, r);
    BOOST_CHECK(!Sample(g, r, 1110, -0.5f, true, false));
    BOOST_CHECK(!Sample(g, r, 1120, -0.5f));
    BOOST_CHECK(!Sample(g, r, 1130, -0.5f, false));
    BOOST_CHECK(!Sample(g, r, 1140, -0.5f));
    BOOST_CHECK(Sample(g, r, 1150, -0.5f, false));
    BOOST_CHECK_SMALL(Gesture::Length(r.velocity), 0.001f);
}

BOOST_AUTO_TEST_CASE(HitchesBackwardsTimeAndTrackingJumpsDiscardHistory)
{
    for (int kind = 0; kind < 4; ++kind)
    {
        Gesture g;
        Release r;
        Stroke(g, r);
        const int time = kind == 0 ? 1400 : kind == 1 ? 900 : 1110;
        const float z = kind == 2 ? -10.0f : kind == 3 ?
                std::numeric_limits<float>::quiet_NaN() : -0.5f;
        BOOST_CHECK(!Sample(g, r, time, z));
        BOOST_CHECK(!Sample(g, r, time+10, -0.5f, false));
    }
}

BOOST_AUTO_TEST_CASE(DuplicateTimestampsAndShortPressAreFinite)
{
    Gesture g;
    Release r;
    Sample(g, r, 1000, -0.3f);
    Sample(g, r, 1000, -0.3f);
    BOOST_REQUIRE(Sample(g, r, 1000, -0.3f, false));
    BOOST_CHECK_SMALL(Gesture::Length(r.velocity), 0.001f);
}

BOOST_AUTO_TEST_CASE(SidearmAndUnderhandDirectionsArePreserved)
{
    for (const Gesture::Vec direction : {Gesture::Vec{1, 0, 0}, {0, 0.6f, -0.8f}})
    {
        Gesture g;
        Release r;
        for (int i = 0; i <= 10; ++i)
        {
            Gesture::Vec position{};
            for (int a = 0; a < 3; ++a) position[a] = direction[a]*(0.3f+i*0.02f);
            g.Update(1000+i*10, position, position, i != 10, true, 250, r);
        }
        for (int a = 0; a < 3; ++a)
            BOOST_CHECK_SMALL(r.velocity[a] - 2*direction[a], 0.001f);
    }
}

BOOST_AUTO_TEST_CASE(LegacyFallbackRejectsInvalidTimingAndTracking)
{
    const float newer[] = {0, 0, -0.4f}, older[] = {0, 0, -0.3f};
    float velocity[3];
    JKXR_ThermalHistoryVelocity(newer, older, 50, velocity);
    BOOST_CHECK_CLOSE(velocity[2], -2.0f, 0.001f);
    for (float dt : {0.0f, -1.0f, 200.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        JKXR_ThermalHistoryVelocity(newer, older, dt, velocity);
        BOOST_CHECK_EQUAL(velocity[2], 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
