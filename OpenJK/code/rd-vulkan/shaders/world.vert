#version 450
#extension GL_GOOGLE_include_directive : require
#include "deform.glsl"
#include "specular.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec2 inLightmapUv;
layout(location = 4) in vec3 inNormal;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;
layout(location = 2) out float vViewDepth;
layout(location = 3) out vec3 vPosition;
layout(location = 4) out vec3 vNormal;
layout(location = 5) out float vSpecularAlpha;

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
	vec3 position = deformPosition(inPosition, inNormal, inUv.x);
	bool localFog = pc.stageFlags.w >= 15.0 && pc.stageFlags.w < 20.0;
	vColor = inColor;
	vSpecularAlpha = 1.0;
	if (pc.stageFlags.w < 10.0 && pc.stageFlags.x >= 4.0)
	{
		vec4 eye = inverse(pc.mvp) * vec4(0.0, 0.0, -1.0, 0.0);
		vSpecularAlpha = materialSpecularAlpha(position, deformNormal(inNormal),
			eye.xyz / eye.w, deform.specularLight);
	}
	vec2 turbulence = vec2(
		sin(((position.x + position.z) / 1024.0 + pc.stageFlags.z) * 6.28318530718),
		sin((position.y / 1024.0 + pc.stageFlags.z) * 6.28318530718)) * pc.stageFlags.y;
	vec2 generatedUv;
	if (pc.stageFlags.w >= 20.0 || localFog)
	{
		generatedUv = inUv;
	}
	else if (pc.useLightmap > 1.5)
	{
		// The camera origin maps to (0, 0, projection[3][2], 0) in clip
		// space. Its scale cancels after the homogeneous divide, allowing the
		// legacy environment-map calculation to remain in model space.
		vec4 localEyeHomogeneous = inverse(pc.mvp) * vec4(0.0, 0.0, -1.0, 0.0);
		vec3 localEye = localEyeHomogeneous.xyz / localEyeHomogeneous.w;
		vec3 viewer = normalize(localEye - position);
		vec3 normal = normalize(deformNormal(inNormal));
		float reflection = dot(normal, viewer);
		generatedUv = vec2(normal.x * reflection - 0.5 * viewer.x,
			normal.y * reflection - 0.5 * viewer.y);
	}
	else
	{
		generatedUv = mix(inUv, inLightmapUv, pc.useLightmap);
	}
	vUv = localFog ? inUv : generatedUv * pc.uvScale + pc.uvOffset + turbulence;
	gl_Position = pc.mvp * vec4(position, 1.0);
	vViewDepth = abs(gl_Position.w);
	vPosition = position;
	vNormal = deformNormal(inNormal);
}
