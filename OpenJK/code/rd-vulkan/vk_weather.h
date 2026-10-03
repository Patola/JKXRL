/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

struct vk_rain_settings_t
{
    bool enabled = false;
    bool acid = false;
    unsigned count = 0;
    float width = 1.2f;
    float height = 80.0f;
    float speed = 2000.0f;
    float color[3] = {0.5f, 0.5f, 0.5f};

    bool Set(std::string_view command)
    {
        if (command != "acidrain" && command != "rain" &&
            command != "lightrain" && command != "heavyrain") return false;
        *this = {};
        enabled = true;
        count = command == "lightrain" ? 500u : 1000u;
        if (command == "heavyrain") speed = 2800.0f;
        if (command == "acidrain")
        {
            acid = true;
            width = 2.0f;
            color[0] = color[2] = 0.34f;
            color[1] = 0.70f;
        }
        return true;
    }
};

inline bool VK_WeatherContentsOutside(bool solidOrWater, bool inside,
                                     bool outside, bool mapMarksOutside)
{
    if (solidOrWater) return false;
    return mapMarksOutside ? outside : !inside;
}

using vk_weather_vector_t = std::array<float, 3>;

inline void VK_WeatherLowercase(std::string &token)
{
    for (char &c : token) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
}

inline float VK_WeatherLength(const vk_weather_vector_t &v)
{
    return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

inline bool VK_WeatherReadVector(std::istream &input, vk_weather_vector_t &out)
{
    char open = 0, close = 0;
    vk_weather_vector_t value{};
    if (!(input >> open >> value[0] >> value[1] >> value[2] >> close) ||
        open != '(' || close != ')') return false;
    for (float component : value)
        if (!std::isfinite(component) || std::fabs(component) > 1000000.0f) return false;
    out = value;
    return true;
}

enum class vk_weather_command_result_t { Unknown, Invalid, Applied };

// Wind strengths are authored force vectors, not unit directions. Keep one
// deterministic 60 Hz reference simulation, independent of eye count and FPS.
class vk_weather_wind_t
{
    struct zone_t
    {
        vk_weather_vector_t velocity{}, target{}, mins{}, maxs{};
        bool local = false, variable = false, gust = false;
        int remaining = 0;
        uint32_t random = 1;
        float Random()
        {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            return float(random >> 8) / 16777216.0f;
        }
        void Step()
        {
            if (!variable) return;
            if (remaining == 0)
            {
                const bool calm = Random() < (gust ? 0.5f : 0.3f);
                const int low = calm && gust ? 2000 : 1000;
                const int high = calm ? (gust ? 4000 : 3000) : (gust ? 3000 : 2000);
                remaining = low + int(Random() * (high - low));
                for (int axis = 0; axis < 3; ++axis)
                    target[axis] = calm ? 0.0f : (Random()*2 - 1) *
                        (axis == 2 ? (gust ? 100.0f : 10.0f) : (gust ? 3000.0f : 1500.0f));
                return;
            }
            --remaining;
            vk_weather_vector_t delta{};
            for (int axis = 0; axis < 3; ++axis) delta[axis] = target[axis] - velocity[axis];
            const float scale = 10.0f / std::max(10.0f, VK_WeatherLength(delta));
            for (int axis = 0; axis < 3; ++axis) velocity[axis] += delta[axis] * scale;
        }
    };
    std::vector<zone_t> zones;
    int lastTime = -1;
    int remainder = 0;
public:
    vk_weather_command_result_t Add(std::string_view command)
    {
        std::istringstream input{std::string(command)};
        input.imbue(std::locale::classic());
        std::string name;
        input >> name;
        VK_WeatherLowercase(name);
        if (name != "constantwind" && name != "windzone" && name != "wind" && name != "gustingwind")
            return vk_weather_command_result_t::Unknown;
        if (zones.size() == 12) return vk_weather_command_result_t::Invalid;
        zone_t zone;
        zone.random = 0x12345678u + uint32_t(zones.size()) * 0x9e3779b9u;
        zone.local = name == "windzone";
        zone.variable = name == "wind" || name == "gustingwind";
        zone.gust = name == "gustingwind";
        if (zone.local)
        {
            if (!VK_WeatherReadVector(input, zone.mins) || !VK_WeatherReadVector(input, zone.maxs))
                return vk_weather_command_result_t::Invalid;
            for (int axis = 0; axis < 3; ++axis)
                if (zone.mins[axis] >= zone.maxs[axis]) return vk_weather_command_result_t::Invalid;
        }
        if (!zone.variable)
        {
            zone.velocity = {0, 800, 0};
            input >> std::ws;
            if (!input.eof() && !VK_WeatherReadVector(input, zone.velocity))
                return vk_weather_command_result_t::Invalid;
        }
        input >> std::ws;
        if (!input.eof()) return vk_weather_command_result_t::Invalid;
        zones.push_back(zone);
        return vk_weather_command_result_t::Applied;
    }

    // Same timestamp is a no-op (second eye / paused scene). Bound catch-up
    // after loading; backwards game clocks establish a fresh time origin.
    float Advance(int milliseconds)
    {
        if (lastTime < 0 || milliseconds < lastTime)
        {
            lastTime = milliseconds;
            remainder = 0;
            return 0;
        }
        const int elapsed = int(std::min<int64_t>(int64_t(milliseconds) - lastTime, 250));
        lastTime = milliseconds;
        remainder += elapsed * 60;
        while (remainder >= 1000)
        {
            for (auto &zone : zones) zone.Step();
            remainder -= 1000;
        }
        return elapsed * 0.001f;
    }

    vk_weather_vector_t Velocity(const float *point = nullptr) const
    {
        vk_weather_vector_t result{};
        for (const auto &zone : zones)
        {
            bool contains = !zone.local || point != nullptr;
            if (zone.local && point)
                for (int axis = 0; axis < 3; ++axis)
                    contains = contains && point[axis] > zone.mins[axis] && point[axis] < zone.maxs[axis];
            if (contains)
                for (int axis = 0; axis < 3; ++axis) result[axis] += zone.velocity[axis];
        }
        return result;
    }
    vk_weather_vector_t Direction(const float *point = nullptr) const
    {
        auto result = Velocity(point);
        const float length = VK_WeatherLength(result);
        if (length > 0) for (float &component : result) component /= length;
        return result;
    }
    bool Gusting(const float *point = nullptr) const { return VK_WeatherLength(Velocity(point)) > 1000; }
    size_t Count() const { return zones.size(); }
};

struct vk_weather_layer_settings_t
{
    enum kind_t { Rain, Snow, Mist, Sand, SpaceDust } kind = Snow;
    std::string name;
    unsigned count = 1000;
    bool acid = false;
    float width = 1.5f, height = 1.5f, fallSpeed = 300;
    float massMin = 5, massMax = 10;
    float opacity = 1;
    float color[3] = {0.875f, 0.875f, 0.875f};
    bool Additive() const { return kind != Sand; }
    bool BroadCloud() const { return kind == Mist || kind == Sand; }
    bool WaterParticles() const { return kind == Rain || kind == Snow || kind == SpaceDust; }
    const char *Texture() const
    {
        if (kind == SpaceDust) return "gfx/effects/snowpuff1.tga";
        return kind == Rain ? "gfx/world/rain.jpg" : kind == Snow ?
            "gfx/effects/snowflake1.tga" : "gfx/effects/alpha_smoke2b.tga";
    }
    const char *FallbackTexture() const
    {
        // JKO's retail assets lack the later JKA weather textures.
        return kind == Sand ? "gfx/effects/alpha_smoke2.tga" :
            kind == SpaceDust ? "gfx/effects/whiteflare.jpg" : nullptr;
    }
    float HorizontalSpan() const { return kind == SpaceDust ? 3000.0f : 1375.0f; }
    float VerticalSpan() const { return BroadCloud() ? 300.0f : kind == SpaceDust ? 3000.0f : 1250.0f; }
    float FadeStart() const { return kind == SpaceDust ? 1200.0f : 500.0f; }
    float FadeEnd() const { return kind == SpaceDust ? 1500.0f : 700.0f; }
};

class vk_weather_layers_t
{
public:
    static constexpr size_t Capacity = 5; // Legacy cloud limit, now independent batches.
    std::vector<vk_weather_layer_settings_t> layers;

    float SaberFizzChance() const
    {
        float gravity = 0;
        unsigned waterLayers = 0;
        for (const auto &layer : layers)
        {
            if (!layer.WaterParticles() || !layer.count) continue;
            gravity += layer.fallSpeed;
            ++waterLayers;
        }
        // Legacy also marks zero-gravity space dust as water; sand/mist are not.
        // This is a gameplay probability query, never a per-eye simulation tick.
        return waterLayers ? std::clamp(gravity / (20000.0f * waterLayers), 0.0f, 1.0f) : 0.0f;
    }
    vk_weather_command_result_t Add(std::string_view command)
    {
        std::istringstream input{std::string(command)};
        input.imbue(std::locale::classic());
        vk_weather_layer_settings_t layer;
        input >> layer.name;
        VK_WeatherLowercase(layer.name);
        vk_rain_settings_t rain;
        if (rain.Set(layer.name))
        {
            layer.kind = vk_weather_layer_settings_t::Rain;
            layer.acid = rain.acid;
            layer.count = rain.count;
            layer.width = rain.width; layer.height = rain.height; layer.fallSpeed = rain.speed;
            std::copy(std::begin(rain.color), std::end(rain.color), layer.color);
        }
        else if (layer.name == "snow" || layer.name == "lightsnow" || layer.name == "heavysnow")
            layer.count = layer.name == "lightsnow" ? 500 : layer.name == "heavysnow" ? 1500 : 1000;
        else if (layer.name == "sand")
        {
            layer.kind = vk_weather_layer_settings_t::Sand;
            layer.count = 400;
            layer.fallSpeed = 0;
            layer.width = layer.height = 140; // Legacy +/-70 billboard extents.
            layer.massMin = 10; layer.massMax = 30;
            layer.color[0] = 0.9f; layer.color[1] = 0.6f; layer.color[2] = 0;
            layer.opacity = 0.5f;
        }
        else if (layer.name == "spacedust")
        {
            layer.kind = vk_weather_layer_settings_t::SpaceDust;
            layer.fallSpeed = 0;
            layer.width = layer.height = 2.4f; // Legacy +/-1.2 billboard extents.
            layer.massMin = 10; layer.massMax = 30;
            for (float &component : layer.color) component = 0.75f * 0.75f;
        }
        else if (layer.name == "fog" || layer.name == "heavyrainfog" || layer.name == "light_fog")
        {
            layer.kind = vk_weather_layer_settings_t::Mist;
            layer.fallSpeed = 0;
            layer.count = layer.name == "fog" ? 60 : layer.name == "heavyrainfog" ? 70 : 40;
            layer.width = layer.height = layer.name == "fog" ? 140 : 200;
            if (layer.name != "heavyrainfog") { layer.massMin = 10; layer.massMax = 30; }
            // Additive ONE/ONE requires premultiplied opacity, as in the legacy cloud draw.
            const float opacity = layer.name == "fog" ? 0.2f : layer.name == "heavyrainfog" ? 0.3f : 0.12f;
            for (float &component : layer.color) component = opacity * opacity;
            if (layer.name == "light_fog")
            {
                layer.color[0] = 0.19f * opacity;
                layer.color[1] = 0.6f * opacity;
                layer.color[2] = 0.7f * opacity;
            }
        }
        else return vk_weather_command_result_t::Unknown;
        input >> std::ws;
        if (input.eof() && layer.kind == vk_weather_layer_settings_t::SpaceDust)
            return vk_weather_command_result_t::Invalid; // Legacy requires a count.
        if (!input.eof())
        {
            std::string token;
            input >> token;
            VK_WeatherLowercase(token);
            if (token == "init") input >> token; // JK2 sends "rain init 500".
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos || token.size() > 6)
                return vk_weather_command_result_t::Invalid;
            const auto count = std::stoul(token);
            if (!count) return vk_weather_command_result_t::Invalid;
            layer.count = unsigned(std::min(count, 4000ul));
            input >> std::ws;
            if (!input.eof()) return vk_weather_command_result_t::Invalid;
        }
        if (layers.size() == Capacity) return vk_weather_command_result_t::Invalid;
        layers.push_back(layer);
        return vk_weather_command_result_t::Applied;
    }
};

// Keep the authored fog untouched. Zero releases the temporary override,
// including after several consecutive flashes; map/reset ownership stays local.
struct vk_weather_fog_flash_t
{
    bool active = false;
    vk_weather_vector_t color{};

    bool Set(const float *requested, bool hasGlobalFog)
    {
        if (!requested || !hasGlobalFog) return false;
        float largest = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(requested[axis]) || requested[axis] < 0 || requested[axis] > 255)
                return false;
            largest = std::max(largest, requested[axis]);
        }
        active = largest > 0;
        // fx_rain's documented/default flashcolor is byte RGB (200 200 200).
        // Also accept normalized callers without boosting or clipping their tint.
        const float scale = largest > 1 ? 1.0f / 255.0f : 1.0f;
        for (int axis = 0; axis < 3; ++axis) color[axis] = requested[axis] * scale;
        return true;
    }

    const float *Color(const float *authored) const { return active ? color.data() : authored; }
};

inline float VK_WeatherWrap(float position, float center, float span)
{
    return position + std::floor((center - position) / span + 0.5f) * span;
}

// Exact integration of drag toward the authored wind's terminal velocity.
// Gravity/fall speeds retain the accepted Vulkan precipitation presets.
inline void VK_WeatherAdvect(vk_weather_vector_t &position, vk_weather_vector_t &velocity,
                            const vk_weather_vector_t &wind, float mass, float fallSpeed, float dt)
{
    constexpr float drag = 21.40049664f; // -60 * log(legacy friction 0.7)
    const float decay = std::exp(-drag * dt);
    for (int axis = 0; axis < 3; ++axis)
    {
        const float target = wind[axis] * (0.7f / (0.3f * mass)) - (axis == 2 ? fallSpeed : 0);
        position[axis] += target * dt + (velocity[axis] - target) * (1 - decay) / drag;
        velocity[axis] = target + (velocity[axis] - target) * decay;
    }
}
