#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec2 inLightmapUv;
layout(location = 4) in vec3 inNormal;

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
	vec2 generatedUv;
	if (pc.useLightmap > 1.5)
	{
		// The camera origin maps to (0, 0, projection[3][2], 0) in clip
		// space. Its scale cancels after the homogeneous divide, allowing the
		// legacy environment-map calculation to remain in model space.
		vec4 localEyeHomogeneous = inverse(pc.mvp) * vec4(0.0, 0.0, -1.0, 0.0);
		vec3 localEye = localEyeHomogeneous.xyz / localEyeHomogeneous.w;
		vec3 viewer = normalize(localEye - inPosition);
		vec3 normal = normalize(inNormal);
		float reflection = dot(normal, viewer);
		generatedUv = vec2(normal.x * reflection - 0.5 * viewer.x,
			normal.y * reflection - 0.5 * viewer.y);
	}
	else
	{
		generatedUv = mix(inUv, inLightmapUv, pc.useLightmap);
	}
	vUv = generatedUv * pc.uvScale + pc.uvOffset + turbulence;
	gl_Position = pc.mvp * vec4(inPosition, 1.0);
	vViewDepth = abs(gl_Position.w);
}
