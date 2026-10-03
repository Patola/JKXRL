// GPL-2.0-or-later. Temporal subset of rd-common/tr_noise.cpp (Id/Raven/OpenJK).
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <random>

namespace vk_material_noise
{
struct Tables
{
	std::array<float,256> values{};
	std::array<uint8_t,256> permutation{};
	Tables()
	{
		// Private, standardized generator: no game RNG mutation, identical on
		// x86-64/ARM64. Preserve legacy interpolation, not its libc-specific table.
		std::minstd_rand random(1001);
		for (int i=0;i<256;++i)
		{
			values[i]=static_cast<float>(double(random()-random.min()) /
				double(random.max()-random.min()) * 2.0 - 1.0);
			permutation[i]=static_cast<uint8_t>(double(random()-random.min()) /
				double(random.max()-random.min()) * 255.0);
		}
	}
	float At(int t) const
	{
		int index=t & 255;
		// INDEX(0,0,0,t) performs four nested permutation lookups.
		for (int i=0;i<4;++i) index=permutation[index];
		return values[index];
	}
};

inline float Sample(float time)
{
	if (!std::isfinite(time)) return 0;
	static const Tables table;
	// Wrap before integer conversion to handle negative and very large times.
	const float wrapped=std::fmod(time,256.0f);
	const float cell=std::floor(wrapped), fraction=wrapped-cell;
	const int index=static_cast<int>(cell);
	return table.At(index)*(1-fraction)+table.At(index+1)*fraction;
}
}
