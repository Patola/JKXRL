#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneDepth;
layout(set = 0, binding = 1) uniform sampler2D shadowDepth;

layout(push_constant) uniform ShadowReceiverPush
{
	mat4 lightViewProjection;
	vec4 eyeProjectionX;
	vec4 forwardProjectionY;
	vec4 leftProjectionOffsetX;
	vec4 upProjectionOffsetY;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out float outShadow;

layout(constant_id = 0) const int shadowFilterMode = 0;

float shadowComparison(vec2 shadowUV, float receiverDepth)
{
	float casterDepth = texture(shadowDepth, shadowUV).r;
	return receiverDepth > casterDepth ? 1.0 : 0.0;
}

void main()
{
	const float zNear = 1.0;
	const float zFar = 65536.0;
	float depth = texture(sceneDepth, vUV).r;
	if (depth >= 0.999999)
	{
		outShadow = 0.0;
		return;
	}

	float distance = (zNear * zFar) /
		(zFar - depth * (zFar - zNear));
	vec2 ndc = vUV * 2.0 - 1.0;
	float viewX = (ndc.x + pc.leftProjectionOffsetX.w) *
		distance / pc.eyeProjectionX.w;
	float viewY = (ndc.y + pc.upProjectionOffsetY.w) *
		distance / pc.forwardProjectionY.w;
	vec3 worldPosition = pc.eyeProjectionX.xyz -
		pc.leftProjectionOffsetX.xyz * viewX +
		pc.upProjectionOffsetY.xyz * viewY +
		pc.forwardProjectionY.xyz * distance;

	vec4 lightClip = pc.lightViewProjection * vec4(worldPosition, 1.0);
	vec3 shadowCoord = lightClip.xyz / lightClip.w;
	vec2 shadowUV = shadowCoord.xy * 0.5 + 0.5;
	if (shadowUV.x <= 0.0 || shadowUV.x >= 1.0 ||
		shadowUV.y <= 0.0 || shadowUV.y >= 1.0 ||
		shadowCoord.z <= 0.0 || shadowCoord.z >= 1.0)
	{
		outShadow = 0.0;
		return;
	}

	float receiverDepth = shadowCoord.z - 0.00035;
	if (shadowFilterMode == 0)
	{
		outShadow = shadowComparison(shadowUV, receiverDepth);
		return;
	}

	if (shadowFilterMode == 1)
	{
		// The accepted compatibility filter remains available for direct A/B.
		const float weights[5] = float[](1.0, 4.0, 6.0, 4.0, 1.0);
		vec2 texel = 1.25 / vec2(textureSize(shadowDepth, 0));
		float filteredShadow = 0.0;
		for (int y = -2; y <= 2; ++y)
		{
			float weightY = weights[y + 2];
			for (int x = -2; x <= 2; ++x)
			{
				float weightX = weights[x + 2];
				filteredShadow += weightX * weightY * shadowComparison(
					shadowUV + vec2(x, y) * texel, receiverDepth);
			}
		}
		outShadow = filteredShadow * (1.0 / 256.0);
		return;
	}

	// A fixed light-space Vogel disk provides a broad, stable penumbra without
	// a screen-space blur. Deriving texel scale from the orthographic matrix
	// keeps softness near two world units as each caster group changes size.
	vec2 textureExtent = vec2(textureSize(shadowDepth, 0));
	vec3 lightRowX = vec3(
		pc.lightViewProjection[0][0],
		pc.lightViewProjection[1][0],
		pc.lightViewProjection[2][0]);
	float worldUnitsPerTexel = 2.0 /
		(max(length(lightRowX), 0.000001) * textureExtent.x);
	float radiusTexels = clamp(2.0 / worldUnitsPerTexel, 4.0, 12.0);
	vec2 texel = 1.0 / textureExtent;
	const int sampleCount = 32;
	const float goldenAngle = 2.39996322973;
	float softShadow = 0.0;
	for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
	{
		float sampleRadius = sqrt((float(sampleIndex) + 0.5) /
			float(sampleCount));
		float sampleAngle = float(sampleIndex) * goldenAngle;
		vec2 sampleOffset = vec2(cos(sampleAngle), sin(sampleAngle)) *
			sampleRadius * radiusTexels * texel;
		softShadow += shadowComparison(
			shadowUV + sampleOffset, receiverDepth);
	}
	outShadow = softShadow / float(sampleCount);
}
