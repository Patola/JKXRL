#version 450

layout(set = 0, binding = 0) uniform sampler2D shadowMask;
layout(set = 0, binding = 1) uniform sampler2D lightReceiverMask;

layout(push_constant) uniform ShadowCompositePush
{
	vec4 parameters;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	float shadow = texture(shadowMask, vUV).r;
	float lightReceiver = texture(lightReceiverMask, vUV).r;
	float receiverScale = mix(1.0, clamp(pc.parameters.y, 0.0, 1.0),
		lightReceiver);
	outColor = vec4(0.0, 0.0, 0.0,
		shadow * clamp(pc.parameters.x, 0.0, 0.8) * receiverScale);
}
