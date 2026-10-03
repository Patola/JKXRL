#version 450
#extension GL_GOOGLE_include_directive : require
#include "local_fog.glsl"

layout(set = 0, binding = 0) uniform sampler2D baseTexture;
layout(set = 2, binding = 0) uniform sampler2D extraLightmaps[3];
layout(set = 2, binding = 1, std140) uniform LightmapStyles
{
	vec4 colors[4];
	vec4 offsets[4];
	vec4 spriteFogPlane;
	vec4 spriteFogColorDepth;
	vec4 spriteFogEye;
} lm;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 2) in float vViewDepth;
layout(location = 3) in vec3 vPosition;
layout(location = 4) in vec3 vNormal;
layout(location = 5) in float vSpecularAlpha;
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
	bool localFog = pc.stageFlags.w >= 15.0 && pc.stageFlags.w < 20.0;
	if (pc.stageFlags.w >= 20.0)
	{
		float alphaTest = pc.lightmapGamma;
		if ((alphaTest > 0.5 && alphaTest < 1.5 && texel.a <= 0.0) ||
			(alphaTest > 1.5 && alphaTest < 2.5 && texel.a >= 0.5) ||
			(alphaTest > 2.5 && alphaTest < 3.5 && texel.a < 0.5) ||
			(alphaTest > 3.5 && texel.a < 0.75))
		{
			discard;
		}
		vec3 lightOrigin = vec3(pc.uvOffset, pc.alpha);
		vec3 toLight = lightOrigin - vPosition;
		vec3 axisScale = max(abs(pc.stageFlags.xyz), vec3(1.0e-6));
		vec3 scaledToLight = toLight * axisScale;
		float distanceSquared = dot(scaledToLight, scaledToLight);
		float radiusSquared = pc.useLightmap * pc.useLightmap;
		if (distanceSquared >= radiusSquared || radiusSquared <= 0.0)
		{
			discard;
		}
		float normalLengthSquared = dot(vNormal, vNormal);
		vec3 scaledNormal = vNormal / axisScale;
		normalLengthSquared = dot(scaledNormal, scaledNormal);
		vec3 normal = normalLengthSquared > 1.0e-8
			? scaledNormal * inversesqrt(normalLengthSquared)
			: vec3(0.0, 0.0, 1.0);
		float facing = max(dot(normal,
			scaledToLight * inversesqrt(max(distanceSquared, 1.0e-8))), 0.0);
		float radial = max(1.0 - distanceSquared / radiusSquared, 0.0);
		float attenuation = radial * radial * smoothstep(0.0, 0.2, facing);
		// Material coverage comes from the completed opaque depth buffer. The
		// model receiver pipeline uses destination-color modulation, so this
		// remains a texture-free light field and cannot expose model UVs.
		outColor = vec4(pc.stageColor.rgb * attenuation, attenuation);
		return;
	}
	if (!localFog && pc.useLightmap > 0.5 && pc.useLightmap < 1.5 && lm.offsets[0].w > 0.5)
	{
		// Accumulate authored lighting before the material's modulation pass.
		texel.rgb = clamp(texel.rgb * lm.colors[0].rgb +
			texture(extraLightmaps[0], vUv + lm.offsets[1].xy).rgb * lm.colors[1].rgb +
			texture(extraLightmaps[1], vUv + lm.offsets[2].xy).rgb * lm.colors[2].rgb +
			texture(extraLightmaps[2], vUv + lm.offsets[3].xy).rgb * lm.colors[3].rgb, 0.0, 1.0);
	}
	if (!localFog && pc.useLightmap > 0.5 && pc.useLightmap < 1.5 &&
		abs(pc.lightmapGamma - 1.0) > 0.0001)
	{
		texel.rgb = pow(max(texel.rgb, vec3(0.0)), vec3(1.0 / pc.lightmapGamma));
	}
	if (pc.stageFlags.w >= 10.0)
	{
		bool alphaWave = pc.stageFlags.x >= 2.0;
		float coverage = alphaWave ? pc.stageColor.a : vColor.a;
		float maskAlpha = texel.a * (alphaWave ? coverage : 1.0);
		float maskMode = pc.stageFlags.w - (localFog ? 15.0 : 10.0);
		if ((maskMode > 0.5 && maskMode < 1.5 && maskAlpha <= 0.0) ||
			(maskMode > 1.5 && maskMode < 2.5 && maskAlpha >= 0.5) ||
			(maskMode > 2.5 && maskMode < 3.5 && maskAlpha < 0.5) ||
			(maskMode > 3.5 && maskAlpha < 0.75))
		{
			discard;
		}
		float fogAmount = clamp(vViewDepth / max(pc.alpha, 1.0), 0.0, 1.0);
		if (pc.lightmapGamma > 0.5)
		{
			fogAmount = clamp((vViewDepth - pc.padding) / max(pc.alpha - pc.padding, 1.0), 0.0, 1.0);
		}
		if (localFog)
		{
			float pointDepth = dot(vec3(pc.uvOffset, pc.useLightmap), vPosition) + pc.padding;
			fogAmount = localFogAmount(vViewDepth, pointDepth, pc.stageFlags.y, pc.alpha);
		}
		if (coverage <= 0.0)
		{
			discard;
		}
		float textureCoverage = localFog && pc.stageFlags.z < 0.5 ? 1.0 : texel.a;
		outColor = vec4(pc.stageColor.rgb, fogAmount * textureCoverage * coverage);
		return;
	}
	bool specularAlpha = pc.stageFlags.x >= 4.0;
	float colorFlags = pc.stageFlags.x - (specularAlpha ? 4.0 : 0.0);
	bool alphaWave = colorFlags >= 2.0;
	float vertexColorWeight = colorFlags - (alphaWave ? 2.0 : 0.0);
	vec4 generatedColor = mix(vec4(1.0), vColor, vertexColorWeight);
	if (alphaWave) generatedColor.a = 1.0;
	if (specularAlpha) generatedColor.a = vSpecularAlpha;
	if (pc.padding > 0.5 && pc.padding < 1.5)
	{
		texel.rgb = vec3(1.0);
	}
	else if (pc.padding > 1.5)
	{
		generatedColor.rgb = vec3(1.0);
	}
	float fragmentAlpha = clamp(
		texel.a * pc.stageColor.a * generatedColor.a * pc.alpha, 0.0, 1.0);
	float alphaTest = pc.stageFlags.w;
	if (alphaTest > 4.5 && alphaTest < 6.5)
	{
		// Plant-only distance coverage: keep the leaf cutout independent of fading.
		float materialAlpha = clamp(texel.a * pc.stageColor.a * pc.alpha, 0.0, 1.0);
		float cutoff = alphaTest < 5.5 ? 0.5 : 0.75;
		if (materialAlpha < cutoff)
		{
			discard;
		}
		if (generatedColor.a < 1.0)
		{
			// Texture-anchored, not screen-space: both eyes and head motion see the same holes.
			const int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
			ivec2 cell = ivec2(floor(vUv * vec2(textureSize(baseTexture, 0)))) & ivec2(3);
			float threshold = (float(bayer[cell.y * 4 + cell.x]) + 0.5) / 16.0;
			if (generatedColor.a <= threshold)
			{
				discard;
			}
		}
		// Discarded coverage writes neither color nor depth; surviving texels retain their alpha.
		generatedColor.a = 1.0;
		fragmentAlpha = materialAlpha;
	}
	if ((alphaTest > 0.5 && alphaTest < 1.5 && fragmentAlpha <= 0.0) ||
		(alphaTest > 1.5 && alphaTest < 2.5 && fragmentAlpha >= 0.5) ||
		(alphaTest > 2.5 && alphaTest < 3.5 && fragmentAlpha < 0.5) ||
		(alphaTest > 3.5 && alphaTest < 4.5 && fragmentAlpha < 0.75))
	{
		discard;
	}
	outColor = texel * pc.stageColor * generatedColor;
	outColor.a = fragmentAlpha;
	if (lm.spriteFogColorDepth.a > 0.0)
	{
		float pointDepth = dot(lm.spriteFogPlane, vec4(vPosition, 1.0));
		float amount = localFogAmount(vViewDepth, pointDepth, lm.spriteFogEye.x, lm.spriteFogColorDepth.a);
		// Fold the legacy equal-depth fog overlay into the leaf's original alpha blend.
		// Cutout and distance-coverage discards have already happened above.
		if (lm.spriteFogEye.y > 0.5)
			outColor.rgb = mix(outColor.rgb, lm.spriteFogColorDepth.rgb, amount);
		else if (amount > 0.0)
		{
			float alpha = outColor.a + (1.0 - outColor.a) * amount;
			outColor.rgb = (outColor.rgb * outColor.a * (1.0 - amount) + lm.spriteFogColorDepth.rgb * amount) / alpha;
			outColor.a = alpha;
		}
	}
}
