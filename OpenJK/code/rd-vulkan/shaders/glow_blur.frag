#version 450

layout(set = 0, binding = 0) uniform sampler2D glowTexture;

layout(push_constant) uniform GlowBlurPush
{
	vec4 parameters;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	vec2 direction = pc.parameters.xy;
	float radius = max(pc.parameters.z, 0.0);
	vec2 offset1 = direction * (1.3846153846 * radius);
	vec2 offset2 = direction * (3.2307692308 * radius);
	vec4 color = texture(glowTexture, vUV) * 0.2270270270;
	color += texture(glowTexture, vUV + offset1) * 0.3162162162;
	color += texture(glowTexture, vUV - offset1) * 0.3162162162;
	color += texture(glowTexture, vUV + offset2) * 0.0702702703;
	color += texture(glowTexture, vUV - offset2) * 0.0702702703;
	outColor = color;
}
