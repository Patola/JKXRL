#version 450
#extension GL_GOOGLE_include_directive : require
#include "../vk_quest_color_matrix.h"

layout(set = 0, binding = 0) uniform sampler2D sceneTexture;
layout(location = 0) out vec4 outColor;

vec3 decodeSRGB(vec3 value)
{
    return mix(pow((value + 0.055) / 1.055, vec3(2.4)), value / 12.92,
        lessThanEqual(value, vec3(0.04045)));
}

vec3 encodeSRGB(vec3 value)
{
    return mix(1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055, value * 12.92,
        lessThanEqual(value, vec3(0.0031308)));
}

void main()
{
    // UNORM input/output: legacy bytes are sRGB-encoded, but Quest requested
    // Rec.2020 primaries. Convert once after blending, including HUD and movies.
    vec4 scene = texelFetch(sceneTexture, ivec2(gl_FragCoord.xy), 0);
    vec3 linearColor = decodeSRGB(scene.rgb);
    vec3 converted = vec3(dot(vec3(VK_QUEST_COLOR_ROW_R), linearColor),
        dot(vec3(VK_QUEST_COLOR_ROW_G), linearColor),
        dot(vec3(VK_QUEST_COLOR_ROW_B), linearColor));
    outColor = vec4(encodeSRGB(clamp(converted, 0.0, 1.0)), scene.a);
}
