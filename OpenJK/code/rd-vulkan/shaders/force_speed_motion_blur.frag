#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform MotionBlurPush
{
	vec4 parameters;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	vec3 color = texture(sceneColor, vUV).rgb;
	outColor = vec4(color, clamp(pc.parameters.x, 0.0, 1.0));
}
