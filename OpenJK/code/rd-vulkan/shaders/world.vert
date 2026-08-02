#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec2 inLightmapUv;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;
layout(location = 2) out float vViewDepth;

layout(push_constant) uniform WorldPush
{
	mat4 mvp;
	vec2 uvOffset;
	float alpha;
	float useLightmap;
	vec4 stageColor;
	vec4 stageFlags;
	vec2 uvScale;
} pc;

void main()
{
	vColor = inColor;
	vec2 turbulence = vec2(
		sin(((inPosition.x + inPosition.z) / 1024.0 + pc.stageFlags.z) * 6.28318530718),
		sin((inPosition.y / 1024.0 + pc.stageFlags.z) * 6.28318530718)) * pc.stageFlags.y;
	vUv = mix(inUv, inLightmapUv, pc.useLightmap) * pc.uvScale + pc.uvOffset + turbulence;
	gl_Position = pc.mvp * vec4(inPosition, 1.0);
	vViewDepth = abs(gl_Position.w);
}
