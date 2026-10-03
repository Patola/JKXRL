#include <boost/test/unit_test.hpp>
#include <limits>
#include "../code/rd-vulkan/vk_view_fog.h"
#include "../code/rd-vulkan/vk_optical_zoom.h"

BOOST_AUTO_TEST_SUITE(ViewFog)
BOOST_AUTO_TEST_CASE(OpticalMagnificationTracksFovAndResetsWhenInactive)
{
	BOOST_CHECK_EQUAL(VK_OpticalZoomScale(false, 3, 108), 1);
	BOOST_CHECK_EQUAL(VK_OpticalZoomScale(true, 108, 108), 1);
	BOOST_CHECK_EQUAL(VK_OpticalZoomScale(true, 120, 108), 1);
	BOOST_CHECK_EQUAL(VK_OpticalZoomScale(true, 0, 108), 1);
	BOOST_CHECK_EQUAL(VK_OpticalZoomScale(true, 3, 0), 1);
	BOOST_CHECK_EQUAL(VK_OpticalZoomScale(true, std::numeric_limits<float>::quiet_NaN(), 108), 1);
	const float wide = VK_OpticalZoomScale(true, 80, 108);
	const float medium = VK_OpticalZoomScale(true, 40, 108);
	const float narrow = VK_OpticalZoomScale(true, 3, 108);
	BOOST_CHECK(wide < 1 && medium < wide && narrow < medium);
	BOOST_CHECK_CLOSE(VK_OpticalZoomScale(true, 60, 90), 0.577350269f, 0.001f);
	BOOST_CHECK_EQUAL(VK_OpticalZoomScale(true, 0.001f, 108), 0.01f);
}
static const float grey[3] = {0.4f, 0.5f, 0.6f};

BOOST_AUTO_TEST_CASE(OrdinaryFogUnchangedAndNoFogRemainsAbsent)
{
	const auto fog = VK_ViewFog({}, true, grey, 3000, 12000, 0, 0);
	BOOST_CHECK(fog.enabled);
	BOOST_CHECK(!fog.linearRange);
	BOOST_CHECK_EQUAL(fog.start, 0);
	BOOST_CHECK_EQUAL(fog.end, 3000);
	BOOST_CHECK_EQUAL(fog.color[1], grey[1]);
	BOOST_CHECK(!VK_ViewFog({false, 640}, false, grey, 0, 12000, 0, 0).enabled);
}

BOOST_AUTO_TEST_CASE(ScopedRangeMatchesLegacyAndZoomReducesNearFog)
{
	const auto wide = VK_ViewFog({false, 6400}, true, grey, 8000, 12000, 0, 0);
	const auto narrow = VK_ViewFog({false, 640}, true, grey, 8000, 12000, 0, 0);
	BOOST_CHECK(wide.linearRange);
	BOOST_CHECK_EQUAL(wide.start, 5600);
	BOOST_CHECK_EQUAL(narrow.start, 8000);
	BOOST_CHECK_EQUAL(wide.end, 12000);
	BOOST_CHECK_EQUAL(narrow.end, 12000);
	const auto huge = VK_ViewFog({false, 99999}, true, grey, 8000, 12000, 0, 0);
	BOOST_CHECK_EQUAL(huge.start, 16);
	const auto hothWide = VK_ViewFog({false, 5120}, true, grey, 1800, 1850, 0, 0);
	const auto hothNarrow = VK_ViewFog({false, 320}, true, grey, 1800, 1850, 0, 0);
	BOOST_CHECK_EQUAL(hothWide.start, 16);
	BOOST_CHECK_EQUAL(hothNarrow.start, 1530);
	BOOST_CHECK_EQUAL(hothNarrow.end, 1850);
}

BOOST_AUTO_TEST_CASE(DesignerStartRestoredWithoutMutatingAuthoredFog)
{
	const auto base = VK_ViewFog({}, true, grey, 8000, 12000, -1000, 0);
	const auto scope = VK_ViewFog({false, 640}, true, grey, 8000, 12000, -1000, 0);
	const auto restored = VK_ViewFog({}, true, grey, 8000, 12000, -1000, 0);
	BOOST_CHECK_EQUAL(base.start, 1000);
	BOOST_CHECK_EQUAL(base.end, 8000);
	BOOST_CHECK_EQUAL(scope.end, 12000);
	BOOST_CHECK_EQUAL(restored.start, base.start);
	BOOST_CHECK_EQUAL(restored.end, base.end);
	BOOST_CHECK_EQUAL(VK_ViewFog({}, true, grey, 8000, 12000, -9000, 0).start, 7999);
}

BOOST_AUTO_TEST_CASE(GogglesHaveSameWarmTintInBothEyesAndDoNotOverwriteBase)
{
	const auto left = VK_ViewFog({true, 0}, false, grey, 3000, 12000, 0, 1250);
	const auto right = VK_ViewFog({true, 0}, false, grey, 3000, 12000, 0, 1250);
	BOOST_CHECK(left.enabled);
	BOOST_CHECK_EQUAL(left.end, 10000);
	BOOST_CHECK(left.color == right.color);
	BOOST_CHECK_EQUAL(left.color[0], 0.75f);
	BOOST_CHECK(left.color[1] >= 0.42f && left.color[1] <= 0.445f);
	const auto off = VK_ViewFog({}, true, grey, 3000, 12000, 0, 1250);
	BOOST_CHECK_EQUAL(off.color[0], grey[0]);
	BOOST_CHECK_EQUAL(off.end, 3000);
}

BOOST_AUTO_TEST_CASE(SceneFrameMenuAndLevelStateIsolation)
{
	vk_view_fog_state_t pending;
	pending.goggles = true;
	pending.SetRange(640);
	const auto world = pending.Snapshot(true);
	BOOST_CHECK(!pending.Snapshot(false).goggles);
	BOOST_CHECK_EQUAL(pending.Snapshot(false).range, 0);
	pending.ClearScene();
	BOOST_CHECK(!pending.goggles);
	BOOST_CHECK_EQUAL(pending.range, 640);
	BOOST_CHECK(world.goggles);
	pending.BeginFrame();
	BOOST_CHECK_EQUAL(pending.range, 0);
	pending.SetRange(std::numeric_limits<float>::quiet_NaN());
	BOOST_CHECK_EQUAL(pending.range, 0);
	pending.goggles = true;
	pending = {};
	BOOST_CHECK(!pending.goggles);
}
BOOST_AUTO_TEST_SUITE_END()
