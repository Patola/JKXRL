/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <boost/test/unit_test.hpp>
#include <limits>
#include "../code/rd-vulkan/vk_weather.h"

BOOST_AUTO_TEST_SUITE(VulkanWeather)

BOOST_AUTO_TEST_CASE(AcidRainUsesLegacyGreenStreakPreset)
{
    vk_rain_settings_t rain;
    BOOST_REQUIRE(rain.Set("acidrain"));
    BOOST_CHECK(rain.enabled);
    BOOST_CHECK(rain.acid);
    BOOST_CHECK_EQUAL(rain.count, 1000u);
    BOOST_CHECK_EQUAL(rain.height, 80.0f);
    BOOST_CHECK_EQUAL(rain.width, 2.0f);
    BOOST_CHECK_EQUAL(rain.speed, 2000.0f);
    BOOST_CHECK_CLOSE(rain.color[0], 0.34f, 0.001f);
    BOOST_CHECK_CLOSE(rain.color[1], 0.70f, 0.001f);
    BOOST_CHECK_EQUAL(rain.color[2], rain.color[0]);
}

BOOST_AUTO_TEST_CASE(NonAcidPresetsAndReset)
{
    vk_rain_settings_t rain;
    rain.Set("acidrain");
    BOOST_REQUIRE(rain.Set("lightrain"));
    BOOST_CHECK(!rain.acid);
    BOOST_CHECK_EQUAL(rain.count, 500u);
    BOOST_CHECK_EQUAL(rain.width, 1.2f);
    BOOST_CHECK(rain.Set("heavyrain"));
    BOOST_CHECK_EQUAL(rain.speed, 2800.0f);
    BOOST_CHECK(!rain.Set("heavyrainfog"));
    BOOST_CHECK_EQUAL(rain.speed, 2800.0f);
    BOOST_CHECK(rain.Set("rain"));
    BOOST_CHECK_EQUAL(rain.speed, 2000.0f);
    rain = {};
    BOOST_CHECK(!rain.enabled);
    BOOST_CHECK(!rain.acid);
    BOOST_CHECK_EQUAL(rain.count, 0u);
}

BOOST_AUTO_TEST_CASE(InsideMarkedMapsExcludeShelter)
{
    BOOST_CHECK(VK_WeatherContentsOutside(false, false, false, false));
    BOOST_CHECK(!VK_WeatherContentsOutside(false, true, false, false));
    BOOST_CHECK(!VK_WeatherContentsOutside(true, false, false, false));
    BOOST_CHECK(!VK_WeatherContentsOutside(false, true, true, false));
}

BOOST_AUTO_TEST_CASE(OutsideMarkedMapsRequireExplicitExposure)
{
    BOOST_CHECK(!VK_WeatherContentsOutside(false, false, false, true));
    BOOST_CHECK(VK_WeatherContentsOutside(false, false, true, true));
    BOOST_CHECK(!VK_WeatherContentsOutside(true, false, true, true));
}

BOOST_AUTO_TEST_CASE(RainRemainsAnchoredDuringHeadTranslation)
{
    vk_weather_vector_t position{0, 0, 175}, velocity{0, 0, -2000};
    const float start = VK_WeatherWrap(position[2], 100, 1250);
    BOOST_CHECK_EQUAL(start, VK_WeatherWrap(position[2], 110, 1250));
    BOOST_CHECK_EQUAL(start, VK_WeatherWrap(position[2], 90, 1250));
    VK_WeatherAdvect(position, velocity, {}, 10, 2000, 0.01f);
    BOOST_CHECK_CLOSE(start - position[2], 20, 0.001f);
}

BOOST_AUTO_TEST_CASE(VerticalWrappingStaysInNearbyWorldTile)
{
    for (float view : {-3000.0f, -100.0f, 0.0f, 100.0f, 3000.0f})
        for (float time : {0.0f, 1.0f, 600.0f})
            BOOST_CHECK(std::fabs(VK_WeatherWrap(875 - time * 2000, view, 1250) - view) <= 625);
}

BOOST_AUTO_TEST_CASE(IndependentLayersKeepPrecipitationAndMist)
{
    vk_weather_layers_t weather;
    using R = vk_weather_command_result_t;
    BOOST_REQUIRE(weather.Add("heavyrain") == R::Applied);
    BOOST_REQUIRE(weather.Add("heavyrainfog") == R::Applied);
    BOOST_REQUIRE(weather.Add("snow init 1500") == R::Applied);
    BOOST_REQUIRE(weather.Add("light_fog") == R::Applied);
    BOOST_REQUIRE(weather.Add("acidrain") == R::Applied);
    BOOST_REQUIRE_EQUAL(weather.layers.size(), 5);
    BOOST_CHECK(weather.layers[0].kind == vk_weather_layer_settings_t::Rain);
    BOOST_CHECK(weather.layers[1].kind == vk_weather_layer_settings_t::Mist);
    BOOST_CHECK(weather.layers[2].kind == vk_weather_layer_settings_t::Snow);
    BOOST_CHECK_EQUAL(weather.layers[2].count, 1500);
    BOOST_CHECK(weather.layers[4].acid);
    BOOST_CHECK(weather.Add("snow") == R::Invalid);
    BOOST_CHECK_EQUAL(weather.layers.size(), 5);
    weather = {};
    BOOST_CHECK(weather.layers.empty());
    BOOST_REQUIRE(weather.Add("rain init 500") == R::Applied);
    BOOST_CHECK_EQUAL(weather.layers[0].count, 500);
    BOOST_CHECK(!weather.layers[0].acid);
}

BOOST_AUTO_TEST_CASE(WeatherArgumentsAreAtomicBoundedAndExact)
{
    vk_weather_layers_t weather;
    using R = vk_weather_command_result_t;
    for (const char *command : {"rain 0", "snow -1", "rain init", "snow nan", "rain 1234567890", "fog 20 garbage"})
        BOOST_CHECK(weather.Add(command) == R::Invalid);
    BOOST_CHECK(weather.layers.empty());
    BOOST_CHECK(weather.Add("rainbow") == R::Unknown);
    BOOST_REQUIRE(weather.Add("snow 999999") == R::Applied);
    BOOST_CHECK_EQUAL(weather.layers[0].count, 4000);
    BOOST_REQUIRE(weather.Add("fog") == R::Applied);
    BOOST_REQUIRE(weather.Add("light_fog") == R::Applied);
    BOOST_CHECK_EQUAL(weather.layers[1].count, 60);
    BOOST_CHECK_EQUAL(weather.layers[2].count, 40);
    BOOST_CHECK_CLOSE(weather.layers[2].color[1], 0.6f * 0.12f, 0.001f);
    BOOST_REQUIRE(weather.Add("RAIN INIT 500") == R::Applied);
    BOOST_CHECK_EQUAL(weather.layers.back().count, 500);
}

BOOST_AUTO_TEST_CASE(WindSumsRawVectorsThenNormalizesGameplayQuery)
{
    vk_weather_wind_t wind;
    using R = vk_weather_command_result_t;
    BOOST_CHECK_EQUAL(VK_WeatherLength(wind.Direction()), 0);
    BOOST_CHECK(!wind.Gusting());
    BOOST_REQUIRE(wind.Add("constantwind ( 300 0 0 )") == R::Applied);
    BOOST_REQUIRE(wind.Add("constantwind ( 0 400 0 )") == R::Applied);
    BOOST_CHECK_EQUAL(VK_WeatherLength(wind.Velocity()), 500);
    BOOST_CHECK_CLOSE(wind.Direction()[0], 0.6f, 0.001f);
    BOOST_CHECK_CLOSE(wind.Direction()[1], 0.8f, 0.001f);
    BOOST_CHECK(!wind.Gusting());
    BOOST_REQUIRE(wind.Add("constantwind ( -300 -400 0 )") == R::Applied);
    BOOST_CHECK_EQUAL(VK_WeatherLength(wind.Direction()), 0);
    wind = {};
    BOOST_REQUIRE(wind.Add("CONSTANTWIND") == R::Applied);
    BOOST_CHECK_EQUAL(wind.Velocity()[1], 800);
    BOOST_REQUIRE(wind.Add("constantwind ( 0 200 0 )") == R::Applied);
    BOOST_CHECK(!wind.Gusting()); // Strict legacy >1000 threshold.
    BOOST_REQUIRE(wind.Add("constantwind ( 0 1 0 )") == R::Applied);
    BOOST_CHECK(wind.Gusting());
}

BOOST_AUTO_TEST_CASE(LocalWindUsesBoundsAndCombinesWithGlobals)
{
    vk_weather_wind_t wind;
    using R = vk_weather_command_result_t;
    BOOST_REQUIRE(wind.Add("constantwind ( 300 0 0 )") == R::Applied);
    BOOST_REQUIRE(wind.Add("windzone (-10 -10 -10) (10 10 10) (0 400 0)") == R::Applied);
    const float inside[3] = {0,0,0}, boundary[3] = {10,0,0}, outside[3] = {11,0,0};
    BOOST_CHECK_EQUAL(VK_WeatherLength(wind.Velocity(inside)), 500);
    BOOST_CHECK_EQUAL(VK_WeatherLength(wind.Velocity(boundary)), 300);
    BOOST_CHECK_EQUAL(VK_WeatherLength(wind.Velocity(outside)), 300);
    BOOST_CHECK_EQUAL(VK_WeatherLength(wind.Velocity()), 300);
    BOOST_CHECK_CLOSE(wind.Direction(inside)[0], 0.6f, 0.001f);
}

BOOST_AUTO_TEST_CASE(MalformedWindDoesNotOverwriteValidState)
{
    vk_weather_wind_t wind;
    using R = vk_weather_command_result_t;
    BOOST_REQUIRE(wind.Add("constantwind (1 2 3)") == R::Applied);
    for (const char *command : {"constantwind (1 2)", "constantwind (1 2 3", "windzone (0 0 0) (0 1 1)",
        "constantwind (nan 2 3)", "constantwind (1e30 2 3)", "wind junk", "gustingwind junk"})
        BOOST_CHECK(wind.Add(command) == R::Invalid);
    BOOST_CHECK_EQUAL(wind.Count(), 1);
    BOOST_CHECK_EQUAL(wind.Velocity()[2], 3);
    for (int i = 0; i < 11; ++i) BOOST_REQUIRE(wind.Add("wind") == R::Applied);
    BOOST_CHECK(wind.Add("wind") == R::Invalid);
    BOOST_CHECK_EQUAL(wind.Count(), 12);
}

BOOST_AUTO_TEST_CASE(WindTimelineDoesNotDependOnFPSOrEyeCount)
{
    vk_weather_wind_t coarse, fine;
    coarse.Add("wind"); coarse.Add("gustingwind");
    fine = coarse;
    coarse.Advance(0); fine.Advance(0);
    for (int time = 100; time <= 120000; time += 100) coarse.Advance(time);
    for (int time = 10; time <= 120000; time += 10)
    {
        fine.Advance(time);
        BOOST_CHECK_EQUAL(fine.Advance(time), 0); // Other eye must not advance it.
    }
    const auto a = coarse.Velocity(), b = fine.Velocity();
    for (int axis = 0; axis < 3; ++axis) BOOST_CHECK_EQUAL(a[axis], b[axis]);
    BOOST_CHECK(VK_WeatherLength(a) > 0);
    BOOST_CHECK_EQUAL(fine.Advance(100), 0); // Saved game clock moved backwards.
    BOOST_CHECK_EQUAL(fine.Velocity()[0], b[0]);
    BOOST_CHECK_CLOSE(fine.Advance(1000000), 0.25f, 0.001f);
    fine = {};
    BOOST_CHECK_EQUAL(fine.Count(), 0);
    BOOST_CHECK_EQUAL(VK_WeatherLength(fine.Velocity()), 0);
}

BOOST_AUTO_TEST_CASE(ParticleWindAdvectionIsStableAndSpeedSensitive)
{
    vk_weather_vector_t coarse{}, fine{}, vc{}, vf{};
    const vk_weather_vector_t wind{3000, -1000, 0};
    VK_WeatherAdvect(coarse, vc, wind, 10, 2000, 1);
    for (int i = 0; i < 100; ++i) VK_WeatherAdvect(fine, vf, wind, 10, 2000, 0.01f);
    for (int axis = 0; axis < 3; ++axis)
    {
        BOOST_CHECK_CLOSE(coarse[axis], fine[axis], 0.001f);
        BOOST_CHECK_CLOSE(vc[axis], vf[axis], 0.001f);
    }
    vk_weather_vector_t slower{}, vs{};
    VK_WeatherAdvect(slower, vs, {300, -100, 0}, 10, 2000, 1);
    BOOST_CHECK_CLOSE(coarse[0], slower[0] * 10, 0.001f);
    const auto paused = coarse;
    VK_WeatherAdvect(coarse, vc, wind, 10, 2000, 0);
    BOOST_CHECK(coarse == paused);
}

BOOST_AUTO_TEST_CASE(ParticleWrappingNeverFollowsSmallHeadTranslations)
{
    BOOST_CHECK_EQUAL(VK_WeatherWrap(100, 0, 1375), 100);
    BOOST_CHECK_EQUAL(VK_WeatherWrap(100, 20, 1375), 100);
    BOOST_CHECK_EQUAL(VK_WeatherWrap(100, -20, 1375), 100);
    for (float center : {-10000.0f, -150.0f, 0.0f, 150.0f, 10000.0f})
        for (float span : {300.0f, 1250.0f, 1375.0f})
            BOOST_CHECK(std::fabs(VK_WeatherWrap(3000, center, span) - center) <= span/2);
}

BOOST_AUTO_TEST_CASE(FizzMatchesWaterPresetsWithoutMistDilution)
{
    vk_weather_layers_t layers;
    BOOST_CHECK_EQUAL(layers.SaberFizzChance(), 0);
    layers.Add("heavyrainfog");
    BOOST_CHECK_EQUAL(layers.SaberFizzChance(), 0);
    layers.Add("heavyrain");
    BOOST_CHECK_CLOSE(layers.SaberFizzChance(), 0.14f, 0.001f);
    layers.Add("rain");
    BOOST_CHECK_CLOSE(layers.SaberFizzChance(), 0.12f, 0.001f);
    layers = {};
    layers.Add("snow init 500");
    BOOST_CHECK_CLOSE(layers.SaberFizzChance(), 0.015f, 0.001f);
    for (const char *preset : {"rain", "lightrain", "acidrain"})
    {
        layers = {};
        layers.Add(preset);
        BOOST_CHECK_CLOSE(layers.SaberFizzChance(), 0.1f, 0.001f);
        // Queries don't advance simulation or consume another blade's chance.
        for (int i = 0; i < 200; ++i)
            BOOST_CHECK_CLOSE(layers.SaberFizzChance(), 0.1f, 0.001f);
    }
    layers = {};
    BOOST_CHECK_EQUAL(layers.SaberFizzChance(), 0);
}

BOOST_AUTO_TEST_CASE(StormUsesBoundedColorAndDoesNotChangeAuthoredFog)
{
    vk_weather_fog_flash_t flash;
    const float authored[3] = {0.15f, 0.2f, 0.3f};
    const float bright[3] = {200, 200, 200}, tinted[3] = {128, 64, 255};
    const float normalized[3] = {0.5f, 0.25f, 1}, off[3] = {};
    BOOST_CHECK(flash.Color(authored) == authored);
    BOOST_REQUIRE(flash.Set(bright, true));
    for (int i = 0; i < 3; ++i)
        BOOST_CHECK_CLOSE(flash.Color(authored)[i], 200.0f / 255, 0.001f);
    BOOST_REQUIRE(flash.Set(tinted, true));
    BOOST_CHECK_CLOSE(flash.Color(authored)[0], 128.0f / 255, 0.001f);
    BOOST_CHECK_CLOSE(flash.Color(authored)[1], 64.0f / 255, 0.001f);
    BOOST_REQUIRE(flash.Set(normalized, true));
    for (int i = 0; i < 3; ++i)
        BOOST_CHECK_EQUAL(flash.Color(authored)[i], normalized[i]);
    BOOST_REQUIRE(flash.Set(off, true));
    BOOST_CHECK(flash.Color(authored) == authored);
    BOOST_CHECK_EQUAL(authored[0], 0.15f);
    BOOST_CHECK(!flash.active);
}

BOOST_AUTO_TEST_CASE(StormRejectsInvalidInputAndResetsOnLevelChange)
{
    vk_weather_fog_flash_t flash;
    const float bright[3] = {200, 200, 200};
    BOOST_CHECK(!flash.Set(bright, false));
    BOOST_CHECK(!flash.active);
    BOOST_REQUIRE(flash.Set(bright, true));
    const auto saved = flash.color;
    for (float invalid : {-1.0f, 256.0f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()})
    {
        const float bad[3] = {0, invalid, 0};
        BOOST_CHECK(!flash.Set(bad, true));
        BOOST_CHECK(flash.active && flash.color == saved);
    }
    BOOST_CHECK(!flash.Set(nullptr, true));
    flash = {};
    BOOST_CHECK(!flash.active);
    const float nextMap[3] = {0, 0.09f, 0.184f};
    BOOST_CHECK(flash.Color(nextMap) == nextMap);
}

BOOST_AUTO_TEST_CASE(SandAndSpaceDustPreserveDistinctLegacyPresets)
{
    vk_weather_layers_t weather;
    using R = vk_weather_command_result_t;
    BOOST_REQUIRE(weather.Add("sand") == R::Applied);
    BOOST_REQUIRE(weather.Add("spacedust 1200") == R::Applied);
    const auto &sand = weather.layers[0], &dust = weather.layers[1];
    BOOST_CHECK_EQUAL(sand.count, 400);
    BOOST_CHECK(!sand.Additive() && sand.BroadCloud() && !sand.WaterParticles());
    BOOST_CHECK_EQUAL(sand.opacity, 0.5f);
    BOOST_CHECK_EQUAL(sand.width, 140);
    BOOST_CHECK_EQUAL(sand.height, 140);
    BOOST_CHECK_EQUAL(sand.color[0], 0.9f);
    BOOST_CHECK_EQUAL(sand.color[1], 0.6f);
    BOOST_CHECK_EQUAL(sand.color[2], 0);
    BOOST_CHECK_EQUAL(sand.VerticalSpan(), 300);
    BOOST_CHECK_EQUAL(dust.count, 1200);
    BOOST_CHECK(dust.Additive() && !dust.BroadCloud() && dust.WaterParticles());
    BOOST_CHECK_EQUAL(dust.width, 2.4f);
    BOOST_CHECK_EQUAL(dust.height, 2.4f);
    BOOST_CHECK_EQUAL(dust.HorizontalSpan(), 3000);
    BOOST_CHECK_EQUAL(dust.VerticalSpan(), 3000);
    BOOST_CHECK_EQUAL(dust.FadeStart(), 1200);
    BOOST_CHECK_EQUAL(dust.FadeEnd(), 1500);
    for (float channel : dust.color) BOOST_CHECK_EQUAL(channel, 0.5625f);
    for (const auto &layer : weather.layers)
    {
        BOOST_CHECK(!layer.acid);
        BOOST_CHECK_EQUAL(layer.fallSpeed, 0);
        BOOST_CHECK_EQUAL(layer.massMin, 10);
        BOOST_CHECK_EQUAL(layer.massMax, 30);
        vk_weather_vector_t position{10,20,30}, velocity{};
        VK_WeatherAdvect(position, velocity, {}, 20, layer.fallSpeed, 1);
        BOOST_CHECK_EQUAL(position[2], 30); // Neither preset silently becomes snow.
        VK_WeatherAdvect(position, velocity, {300,0,0}, 20, layer.fallSpeed, 1);
        BOOST_CHECK(position[0] > 10);
        BOOST_CHECK_EQUAL(position[1], 20);
        BOOST_CHECK_EQUAL(position[2], 30);
    }
    BOOST_CHECK_EQUAL(std::string(dust.Texture()), "gfx/effects/snowpuff1.tga");
    BOOST_CHECK_EQUAL(std::string(dust.FallbackTexture()), "gfx/effects/whiteflare.jpg");
    BOOST_CHECK_EQUAL(std::string(sand.FallbackTexture()), "gfx/effects/alpha_smoke2.tga");
    BOOST_CHECK_EQUAL(weather.SaberFizzChance(), 0);
    weather.Add("rain");
    BOOST_CHECK_CLOSE(weather.SaberFizzChance(), 0.05f, 0.001f); // Legacy dust counts with gravity zero.
}

BOOST_AUTO_TEST_CASE(NewWeatherCountValidationAndExistingPresetsAreUnchanged)
{
    vk_weather_layers_t weather;
    using R = vk_weather_command_result_t;
    for (const char *bad : {"spacedust", "spacedust 0", "spacedust -100", "spacedust 2 extra", "sand nan"})
        BOOST_CHECK(weather.Add(bad) == R::Invalid);
    BOOST_CHECK(weather.layers.empty());
    BOOST_REQUIRE(weather.Add("SPACEDUST 999999") == R::Applied);
    BOOST_CHECK_EQUAL(weather.layers[0].count, 4000);
    weather = {};
    for (const char *name : {"rain", "snow", "fog", "light_fog", "heavyrainfog"})
    {
        BOOST_REQUIRE(weather.Add(name) == R::Applied);
        const auto &layer = weather.layers.back();
        BOOST_CHECK(layer.Additive());
        BOOST_CHECK_EQUAL(layer.opacity, 1);
        BOOST_CHECK_EQUAL(layer.HorizontalSpan(), 1375);
        BOOST_CHECK_EQUAL(layer.VerticalSpan(), layer.BroadCloud() ? 300 : 1250);
        BOOST_CHECK_EQUAL(layer.FadeStart(), 500);
        BOOST_CHECK_EQUAL(layer.FadeEnd(), 700);
        BOOST_CHECK(layer.FallbackTexture() == nullptr);
    }
    BOOST_CHECK(weather.Add("sand") == R::Invalid);
    BOOST_CHECK_EQUAL(weather.layers.size(), 5);
    weather = {};
    BOOST_CHECK(weather.layers.empty());
}

BOOST_AUTO_TEST_SUITE_END()
