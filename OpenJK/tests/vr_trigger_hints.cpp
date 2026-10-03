/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../JKXR/VrTriggerTouch.h"
#include "../JKXR/VrForceHint.h"
#include <limits>

BOOST_AUTO_TEST_SUITE(VrTriggersAndHints)

BOOST_AUTO_TEST_CASE(OnlyMultiTouchUseButtonsBelongToHandPath)
{
    BOOST_CHECK(VR_IsUseButtonTrigger(true, 4));
    BOOST_CHECK(VR_IsUseButtonTrigger(true, 7));
    BOOST_CHECK(!VR_IsUseButtonTrigger(true, 1));
    BOOST_CHECK(!VR_IsUseButtonTrigger(false, 4));
    // Vjun2: player-only relative+linear updraft and NPC-only push volumes.
    BOOST_CHECK(!VR_IsUseButtonTrigger(false, 21));
    BOOST_CHECK(!VR_IsUseButtonTrigger(false, 28));
    BOOST_CHECK(!VR_IsUseButtonTrigger(false, 0));
}

BOOST_AUTO_TEST_CASE(SweptTouchDeduplicationUsesEntityIdentityNotBoxResultSlot)
{
    VrTriggerTouchSet<1024> touched;
    // First sample: entity 20 occupies slot 0, an ordinary trigger.
    BOOST_CHECK(!touched.Seen(20));
    touched.Mark(20);
    // Next sample: updraft 143 now occupies slot 0, still must be processed.
    BOOST_CHECK(!touched.Seen(143));
    touched.Mark(143);
    // Reordering/overlapping the same entities must not retrigger either one.
    BOOST_CHECK(touched.Seen(143));
    BOOST_CHECK(touched.Seen(20));
    BOOST_CHECK(touched.Seen(-1));
    BOOST_CHECK(touched.Seen(1024));
    touched.Mark(-1);
    touched.Mark(1024);
    VrTriggerTouchSet<1024> nextMove;
    BOOST_CHECK(!nextMove.Seen(143));
}

BOOST_AUTO_TEST_CASE(ForceHintsRespectClassFlagsPowerAndRange)
{
    using Target = VrForceHintTarget;
    BOOST_CHECK(VR_ForceHintEligible(Target::Door, 2, 256, 0, 256));
    BOOST_CHECK(VR_ForceHintEligible(Target::Door, 2, 0, 512, 400));
    BOOST_CHECK(!VR_ForceHintEligible(Target::Door, 0, 512, 512, 50));
    BOOST_CHECK(!VR_ForceHintEligible(Target::None, 3, 512, 512, 50));
    BOOST_CHECK(VR_ForceHintEligible(Target::Static, 1, 256, 0, 100));
    BOOST_CHECK(!VR_ForceHintEligible(Target::Static, 1, 0, 512, 100));
    BOOST_CHECK(VR_ForceHintEligible(Target::Static, 2, 0, 512, 100));
    BOOST_CHECK(!VR_ForceHintEligible(Target::Static, 2, 512, 0, 100));
    BOOST_CHECK(VR_ForceHintEligible(Target::Static, 3, 256, 512, 400));
    BOOST_CHECK(!VR_ForceHintEligible(Target::Static, 3, 256, 512, 513));
    BOOST_CHECK(!VR_ForceHintEligible(Target::Static, 3, 0, 0, 0));
    BOOST_CHECK(!VR_ForceHintEligible(Target::Door, 2, 512, 512, -1));
    BOOST_CHECK(!VR_ForceHintEligible(Target::Door, 2, 512, 512, std::numeric_limits<float>::quiet_NaN()));
}

BOOST_AUTO_TEST_CASE(ForceHintSpriteSizeRemainsBounded)
{
    BOOST_CHECK_EQUAL(VR_ForceHintRadius(24, 90, 0), 2);
    BOOST_CHECK_CLOSE(VR_ForceHintRadius(24, 90, 100), 7.5f, 0.001f);
    BOOST_CHECK_EQUAL(VR_ForceHintRadius(24, 90, 10000), 24);
    BOOST_CHECK_EQUAL(VR_ForceHintRadius(24, std::numeric_limits<float>::infinity(), 100), 2);
}

BOOST_AUTO_TEST_SUITE_END()
