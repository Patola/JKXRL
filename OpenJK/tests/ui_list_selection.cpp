#include <boost/test/unit_test.hpp>
#include "../code/ui/ui_list_selection.h"

BOOST_AUTO_TEST_SUITE(UiListSelection)

BOOST_AUTO_TEST_CASE(CommitBeforeActivation)
{
	ui_list_click_state_t clicks;
	int menu, selected = 0, highlight = 0;
	auto select = [&](int row) { selected = highlight = row; };
	BOOST_CHECK(!clicks.Click(&menu, 4, 1, 1000, 300, select));
	BOOST_CHECK_EQUAL(selected, 4);
	BOOST_CHECK_EQUAL(highlight, 4);
	// A stale feeder cannot cause the second click to activate the old row.
	selected = 0;
	const bool activate = clicks.Click(&menu, 4, 1, 1100, 300, select);
	BOOST_REQUIRE(activate);
	BOOST_CHECK_EQUAL(selected, 4);
	BOOST_CHECK_EQUAL(highlight, 4);
}

BOOST_AUTO_TEST_CASE(DifferentRowsDoNotDoubleClick)
{
	ui_list_click_state_t clicks;
	int menu, selected = 0;
	auto select = [&](int row) { selected = row; };
	BOOST_CHECK(!clicks.Click(&menu, 1, 1, 1000, 300, select));
	BOOST_CHECK(!clicks.Click(&menu, 5, 1, 1050, 300, select));
	BOOST_CHECK_EQUAL(selected, 5);
	BOOST_CHECK(clicks.Click(&menu, 5, 1, 1100, 300, select));
}

BOOST_AUTO_TEST_CASE(DifferentListsAndButtonsDoNotDoubleClick)
{
	ui_list_click_state_t clicks;
	int shell, ingame;
	auto select = [](int) {};
	BOOST_CHECK(!clicks.Click(&shell, 3, 1, 1000, 300, select));
	BOOST_CHECK(!clicks.Click(&ingame, 3, 1, 1050, 300, select));
	BOOST_CHECK(!clicks.Click(&ingame, 3, 2, 1100, 300, select));
	BOOST_CHECK(clicks.Click(&ingame, 3, 2, 1150, 300, select));
}

BOOST_AUTO_TEST_CASE(ClosingAndTripleClicksDoNotRepeatLoad)
{
	ui_list_click_state_t clicks;
	int menu;
	auto select = [](int) {};
	BOOST_CHECK(!clicks.Click(&menu, 3, 1, 1000, 300, select));
	BOOST_CHECK(clicks.Click(&menu, 3, 1, 1050, 300, select));
	BOOST_CHECK(!clicks.Click(&menu, 3, 1, 1100, 300, select));
	clicks = {};
	BOOST_CHECK(!clicks.Click(&menu, 3, 1, 1150, 300, select));
}

BOOST_AUTO_TEST_CASE(ExpiredAndBackwardsTime)
{
	ui_list_click_state_t clicks;
	int menu;
	auto select = [](int) {};
	BOOST_CHECK(!clicks.Click(&menu, 3, 1, 1000, 300, select));
	BOOST_CHECK(!clicks.Click(&menu, 3, 1, 1300, 300, select));
	BOOST_CHECK(!clicks.Click(&menu, 3, 1, 500, 300, select));
	BOOST_CHECK(clicks.Click(&menu, 3, 1, 550, 300, select));
}

BOOST_AUTO_TEST_CASE(EmptyStaleAndInvalidSelections)
{
	BOOST_CHECK(!UI_ListSelectionValid(-1, 5));
	BOOST_CHECK(!UI_ListSelectionValid(0, 0));
	BOOST_CHECK(!UI_ListSelectionValid(0, -1));
	BOOST_CHECK(!UI_ListSelectionValid(5, 5));
	BOOST_CHECK(UI_ListSelectionValid(0, 5));
	BOOST_CHECK(UI_ListSelectionValid(4, 5));
}

BOOST_AUTO_TEST_SUITE_END()
