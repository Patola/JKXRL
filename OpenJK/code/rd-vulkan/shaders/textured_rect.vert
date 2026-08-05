#version 450

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform RectPush
{
	vec4 rect;
	vec4 uv;
	vec4 color;
	vec4 rotation;
	vec4 screenTransform;
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
	vec2 position = mix(pc.rect.xy, pc.rect.zw, corner);
	vec2 relative = position - pc.rotation.zw;
	position = pc.rotation.zw + vec2(
		relative.x * pc.rotation.y - relative.y * pc.rotation.x,
		relative.x * pc.rotation.x + relative.y * pc.rotation.y);
	position.x *= pc.screenTransform.x;
	position += pc.screenTransform.yz;
	gl_Position = vec4(position, 0.0, 1.0);
}
