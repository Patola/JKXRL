#version 450

layout(set = 0, binding = 0) uniform sampler2D glowTexture;

layout(push_constant) uniform GlowCompositePush
{
	vec4 parameters;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	vec3 glow = clamp(texture(glowTexture, vUV).rgb * pc.parameters.x, 0.0, 1.0);
	outColor = vec4(glow, 1.0);
}
