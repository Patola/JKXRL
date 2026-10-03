#include <boost/test/unit_test.hpp>
#include "../code/rd-vulkan/vk_electricity.h"
#include <limits>
#include <vector>

namespace
{
using Lines = std::vector<vk_electricity_segment_t>;
Lines Build(const vk_electricity_t &effect, vk_electricity_stats_t *stats = nullptr)
{
	Lines lines;
	const auto result = VK_BuildElectricity(effect, [&](const auto &line) { lines.push_back(line); });
	if (stats) *stats = result;
	return lines;
}

void EqualPoint(const vec3_t a, const vec3_t b, float tolerance = 0.0003f)
{
	for (int axis = 0; axis < 3; ++axis)
		BOOST_CHECK_SMALL(a[axis] - b[axis], tolerance);
}

vk_electricity_t Example()
{
	vk_electricity_t effect;
	VectorSet(effect.start, 10, -20, 30);
	VectorSet(effect.end, 480, 290, 170);
	effect.radius = 4;
	effect.seed = 97341;
	effect.tapered = true;
	return effect;
}
}

BOOST_AUTO_TEST_SUITE(Electricity)
BOOST_AUTO_TEST_CASE(UnflaggedTrunkKeepsExistingVulkanShape)
{
	const auto effect = Example();
	const auto lines = Build(effect);
	vec3_t direction, side, vertical, previous;
	VectorSubtract(effect.end, effect.start, direction);
	const float distance = VectorNormalize(direction);
	MakeNormalVectors(direction, side, vertical);
	VectorCopy(effect.start, previous);
	const int count = std::clamp(static_cast<int>(distance / 16), 1, 64);
	BOOST_REQUIRE_EQUAL(lines.size(), count);
	for (int i = 1; i <= count; ++i)
	{
		const float fraction = static_cast<float>(i) / count;
		vec3_t expected;
		VectorMA(effect.start, distance * fraction, direction, expected);
		if (i < count)
		{
			const float envelope = std::sin(fraction * 3.14159265359f);
			const float phase = effect.seed * 0.00031f + i * 2.39996323f;
			VectorMA(expected, std::sin(phase) * effect.chaos * 7 * envelope, side, expected);
			VectorMA(expected, std::cos(phase * 1.37f) * effect.chaos * 7 * envelope, vertical, expected);
		}
		EqualPoint(lines[i - 1].start, previous);
		EqualPoint(lines[i - 1].end, expected);
		BOOST_CHECK_EQUAL(lines[i - 1].strand, 0);
		BOOST_CHECK_EQUAL(lines[i - 1].endRadius, effect.radius * (1 - fraction * fraction));
		VectorCopy(expected, previous);
	}
}

BOOST_AUTO_TEST_CASE(ForksAreBoundedAttachedAndDoNotChangeTrunk)
{
	auto effect = Example();
	int totalForks = 0, maxForks = 0;
	for (int seed = 0; seed < 512; ++seed)
	{
		effect.seed = seed;
		effect.forked = false;
		const auto trunk = Build(effect);
		effect.forked = true;
		vk_electricity_stats_t stats;
		const auto lines = Build(effect, &stats);
		BOOST_CHECK_LE(stats.forks, 3);
		BOOST_CHECK_LE(stats.segments, 256);
		BOOST_CHECK_EQUAL(lines.size(), stats.segments);
		totalForks += stats.forks;
		maxForks = std::max(maxForks, stats.forks);
		int trunkIndex = 0;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			const auto &line = lines[i];
			if (!line.strand)
			{
				EqualPoint(line.start, trunk[trunkIndex].start, 0.000001f);
				EqualPoint(line.end, trunk[trunkIndex++].end, 0.000001f);
			}
			else if (line.startUV == 0)
			{
				BOOST_REQUIRE_GT(i, 0);
				const auto &parent = lines[i - 1];
				EqualPoint(line.start, parent.end, 0.000001f);
				BOOST_CHECK_LT(parent.endUV, 0.2f);
				BOOST_CHECK_EQUAL(line.startRadius, parent.endRadius);
			}
			else
			{
				size_t previous = i - 1;
				while (previous && lines[previous].strand != line.strand) --previous;
				BOOST_CHECK_EQUAL(lines[previous].strand, line.strand);
				EqualPoint(line.start, lines[previous].end, 0.000001f);
			}
			if (line.endUV == 1) BOOST_CHECK_EQUAL(line.endRadius, 0);
		}
		BOOST_CHECK_EQUAL(trunkIndex, trunk.size());
	}
	BOOST_CHECK_GT(totalForks, 0);
	BOOST_CHECK_EQUAL(maxForks, 3);
}

BOOST_AUTO_TEST_CASE(DeterministicAcrossEyesAndGlowWithoutMutatingInput)
{
	auto effect = Example();
	effect.forked = true;
	effect.seed = -1;
	const auto original = effect;
	const auto left = Build(effect), right = Build(effect), glow = Build(effect);
	BOOST_REQUIRE_EQUAL(left.size(), right.size());
	BOOST_REQUIRE_EQUAL(left.size(), glow.size());
	for (size_t i = 0; i < left.size(); ++i)
	{
		EqualPoint(left[i].start, right[i].start, 0.000001f);
		EqualPoint(left[i].end, glow[i].end, 0.000001f);
		BOOST_CHECK_EQUAL(left[i].strand, right[i].strand);
		BOOST_CHECK_EQUAL(left[i].endRadius, glow[i].endRadius);
	}
	BOOST_CHECK_EQUAL(effect.seed, original.seed);
	EqualPoint(effect.end, original.end, 0.000001f);

	uint32_t seed = 1;
	BOOST_CHECK_EQUAL(VK_ElectricityRandom(seed), (69070u & 0xffffu) / 65536.0f);
	BOOST_CHECK_EQUAL(seed, 69070u);
}

BOOST_AUTO_TEST_CASE(GrowthDegeneracyAndFiniteGeometry)
{
	auto effect = Example();
	effect.growth = 0;
	BOOST_CHECK(Build(effect).empty());
	effect.growth = 0.5f;
	const auto half = Build(effect);
	BOOST_REQUIRE(!half.empty());
	vec3_t middle;
	for (int i = 0; i < 3; ++i) middle[i] = (effect.start[i] + effect.end[i]) * 0.5f;
	EqualPoint(half.back().end, middle);
	effect.growth = 2;
	EqualPoint(Build(effect).back().end, effect.end);
	effect.forked = true;
	effect.tapered = false;
	effect.end[0] = 1e10f;
	const auto longBolt = Build(effect);
	BOOST_REQUIRE(!longBolt.empty());
	BOOST_CHECK_LE(longBolt.size(), 256);
	for (const auto &line : longBolt)
	{
		BOOST_CHECK_EQUAL(line.startRadius, effect.radius);
		BOOST_CHECK_EQUAL(line.endRadius, effect.radius);
		for (int i = 0; i < 3; ++i) BOOST_CHECK(std::isfinite(line.end[i]));
	}
	effect.end[0] = std::numeric_limits<float>::infinity();
	BOOST_CHECK(Build(effect).empty());
	effect = Example();
	effect.chaos = std::numeric_limits<float>::quiet_NaN();
	BOOST_CHECK(Build(effect).empty());
	effect = Example();
	effect.radius = -1;
	BOOST_CHECK(Build(effect).empty());
	effect = Example();
	VectorCopy(effect.start, effect.end);
	BOOST_CHECK(Build(effect).empty());
}
BOOST_AUTO_TEST_SUITE_END()
