#version 450

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform RectPush
{
	vec4 rect;
	vec4 uv;
	vec4 color;
} pc;

const vec2 corners[6] = vec2[](
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(0.0, 1.0),
	vec2(0.0, 1.0),
	vec2(1.0, 0.0),
	vec2(1.0, 1.0)
);

void main()
{
	vec2 corner = corners[gl_VertexIndex];
	vUv = mix(pc.uv.xy, pc.uv.zw, corner);
	vColor = pc.color;
	gl_Position = vec4(mix(pc.rect.xy, pc.rect.zw, corner), 0.0, 1.0);
}
