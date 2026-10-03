#version 450
#extension GL_GOOGLE_include_directive : require
#include "deform.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUv;
layout(location = 4) in vec3 inNormal;

layout(push_constant) uniform ShadowMapPush
{
	mat4 lightMvp;
} pc;

void main()
{
	gl_Position = pc.lightMvp * vec4(deformPosition(inPosition, inNormal, inUv.x), 1.0);
}
