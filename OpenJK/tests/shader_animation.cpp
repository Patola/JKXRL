/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_shader_animation.h"
#include <limits>

BOOST_AUTO_TEST_SUITE(ShaderAnimation)

BOOST_AUTO_TEST_CASE(ExplicitDoorStateOverridesOneShotClock)
{
    // Shipped Vjun door material: oneShotAnimMap 1 red green.
    for (float seconds : {0.0f, 0.5f, 2.0f, 600.0f})
    {
        BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, seconds, 1, true, 0), 0u);
        BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, seconds, 1, true, 1), 1u);
    }
    // Unlock, then lock again, with no shared material state to retain green.
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, 600, 1, true, 1), 1u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, 601, 1, true, 0), 0u);
}

BOOST_AUTO_TEST_CASE(SharedDoorMaterialAndBothEyesRemainIndependentOfDrawOrder)
{
    for (int eye = 0; eye < 2; ++eye)
        for (int selector : {1, 0, 1, 1, 0})
            BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, 700, 1, true, selector), size_t(selector));
}

BOOST_AUTO_TEST_CASE(UncontrolledAnimationsKeepTheirOriginalTiming)
{
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, 0.5f, 1, true), 0u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, 1, 1, true), 1u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(2, 600, 1, true), 1u);
    for (int step = 0; step < 1000; ++step)
    {
        const float seconds = step * 0.137f;
        const int legacy = int(std::floor(seconds * 7.0f));
        BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(8, seconds, 7, false), size_t(legacy % 8));
        BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(8, seconds, 7, true), size_t(std::min(legacy, 7)));
    }
}

BOOST_AUTO_TEST_CASE(FrameBoundsAreDefined)
{
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(0, 100, 1, false), 0u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(1, 100, 1, true, 8), 0u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(3, 100, 1, true, 8), 2u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(3, 100, 1, false, 8), 2u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(3, -100, 1, false), 0u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(3, 100, -1, false), 0u);
    BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(3, std::numeric_limits<float>::infinity(), 1, false), 0u);
    BOOST_CHECK_LT(VK_ShaderAnimationFrame(3, 2000000, 10000, false), 3u);
}

BOOST_AUTO_TEST_SUITE_END()
