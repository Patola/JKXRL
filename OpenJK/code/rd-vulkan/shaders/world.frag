#version 450

layout(set = 0, binding = 0) uniform sampler2D baseTexture;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 2) in float vViewDepth;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform WorldPush
{
	mat4 mvp;
	vec2 uvOffset;
	float alpha;
	float useLightmap;
	vec4 stageColor;
	vec4 stageFlags;
	vec2 uvScale;
	float lightmapGamma;
	float padding;
} pc;

void main()
{
	vec4 texel = texture(baseTexture, vUv);
	if (pc.useLightmap > 0.5 && pc.useLightmap < 1.5 &&
		abs(pc.lightmapGamma - 1.0) > 0.0001)
	{
		texel.rgb = pow(max(texel.rgb, vec3(0.0)), vec3(1.0 / pc.lightmapGamma));
	}
	if (pc.stageFlags.w >= 10.0)
	{
		float maskMode = pc.stageFlags.w - 10.0;
		if ((maskMode > 0.5 && maskMode < 1.5 && texel.a <= 0.0) ||
			(maskMode > 1.5 && maskMode < 2.5 && texel.a >= 0.5) ||
			(maskMode > 2.5 && maskMode < 3.5 && texel.a < 0.5) ||
			(maskMode > 3.5 && texel.a < 0.75))
		{
			discard;
		}
		float fogAmount = clamp(vViewDepth / max(pc.alpha, 1.0), 0.0, 1.0);
		if (vColor.a <= 0.0)
		{
			discard;
		}
		outColor = vec4(pc.stageColor.rgb, fogAmount * texel.a * vColor.a);
		return;
	}
	vec4 generatedColor = mix(vec4(1.0), vColor, pc.stageFlags.x);
	float fragmentAlpha = clamp(
		texel.a * pc.stageColor.a * generatedColor.a * pc.alpha, 0.0, 1.0);
	float alphaTest = pc.stageFlags.w;
	if ((alphaTest > 0.5 && alphaTest < 1.5 && fragmentAlpha <= 0.0) ||
		(alphaTest > 1.5 && alphaTest < 2.5 && fragmentAlpha >= 0.5) ||
		(alphaTest > 2.5 && alphaTest < 3.5 && fragmentAlpha < 0.5) ||
		(alphaTest > 3.5 && fragmentAlpha < 0.75))
	{
		discard;
	}
	outColor = texel * pc.stageColor * generatedColor;
	outColor.a = fragmentAlpha;
}
