#pragma once

inline bool UI_ListSelectionValid(int row, int count)
{
	return row >= 0 && row < count;
}

struct ui_list_click_state_t
{
	const void *owner = nullptr;
	int row = -1;
	int key = 0;
	int time = 0;

	template<typename Select>
	bool Click(const void *item, int clickedRow, int button, int now, int delay, Select select)
	{
		const bool activate = owner == item && row == clickedRow && key == button &&
			now >= time && static_cast<long long>(now) - time < delay;
		if (activate)
			*this = {};
		else
		{
			owner = item;
			row = clickedRow;
			key = button;
			time = now;
		}
		// The feeder must own the clicked row before an activation script runs.
		select(clickedRow);
		return activate;
	}
};
