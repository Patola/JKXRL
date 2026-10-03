#version 450

layout(location = 0) out vec4 vColor;

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
	vec2 p = mix(pc.rect.xy, pc.rect.zw, corners[gl_VertexIndex]);
	vColor = pc.color;
	gl_Position = vec4(p, 0.0, 1.0);
}
