#version 450

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec2 vOverlayUv;
layout(location = 3) flat out float vOverlayMode;

layout(push_constant) uniform RectPush
{
	vec4 rect;
	vec4 uv;
	vec4 color;
	vec4 rotation;
	vec4 uvRotation;
	vec4 overlayUvTransform;
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
	if (pc.screenTransform.w > 2.5)
	{
		vec2 projectedCorners[4] = vec2[](
			pc.rotation.xy,
			pc.rotation.zw,
			pc.uvRotation.xy,
			pc.uvRotation.zw);
		int cornerIndex = gl_VertexIndex == 0 ? 0 :
			(gl_VertexIndex == 1 || gl_VertexIndex == 4 ? 1 :
			(gl_VertexIndex == 2 || gl_VertexIndex == 3 ? 2 : 3));
		vOverlayUv = corner;
		vOverlayMode = pc.screenTransform.w;
		vUv = mix(pc.uv.xy, pc.uv.zw, corner);
		vColor = pc.color;
		gl_Position = vec4(projectedCorners[cornerIndex], 0.0, 1.0);
		return;
	}
	vec2 overlayCorner = corner * pc.overlayUvTransform.xy + pc.overlayUvTransform.zw;
	if (pc.screenTransform.w > 1.5)
	{
		overlayCorner = vec2(0.5) + (overlayCorner - vec2(0.5)) * pc.uvRotation.z;
	}
	vOverlayUv = corner;
	vOverlayMode = pc.screenTransform.w;
	vec2 uv = mix(pc.uv.xy, pc.uv.zw, overlayCorner);
	vec2 relativeUv = uv - vec2(0.5);
	vUv = vec2(0.5) + vec2(
		relativeUv.x * pc.uvRotation.y - relativeUv.y * pc.uvRotation.x,
		relativeUv.x * pc.uvRotation.x + relativeUv.y * pc.uvRotation.y);
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
