#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform TestPattern
{
	vec4 tint;
} pc;

void main()
{
	vec2 clampedUV = clamp(vUV, vec2(0.0), vec2(1.0));
	float gridX = step(0.965, fract(clampedUV.x * 12.0));
	float gridY = step(0.965, fract(clampedUV.y * 12.0));
	float axisX = 1.0 - smoothstep(0.006, 0.014, abs(clampedUV.x - 0.5));
	float axisY = 1.0 - smoothstep(0.006, 0.014, abs(clampedUV.y - 0.5));
	float border = max(
		step(clampedUV.x, 0.02) + step(0.98, clampedUV.x),
		step(clampedUV.y, 0.02) + step(0.98, clampedUV.y));

	vec3 base = vec3(clampedUV.x, clampedUV.y, 0.32);
	vec3 tinted = mix(base, pc.tint.rgb, 0.42);
	vec3 grid = mix(tinted, vec3(1.0), clamp(gridX + gridY + axisX + axisY + border, 0.0, 1.0) * 0.55);
	outColor = vec4(grid, 1.0);
}
