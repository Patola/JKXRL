#version 450

layout(set = 0, binding = 0) uniform sampler2D colorTexture;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec2 vOverlayUv;
layout(location = 3) flat in float vOverlayMode;
layout(location = 0) out vec4 outColor;

void main()
{
	if (vOverlayMode > 0.5 && vOverlayMode < 1.5)
	{
		vec2 edgeDistance = abs(vOverlayUv * 2.0 - 1.0);
		float superellipse = pow(
			pow(edgeDistance.x, 4.0) + pow(edgeDistance.y, 4.0), 0.25);
		float alpha = smoothstep(0.15, 0.95, superellipse) * 0.45;
		outColor = vec4(vColor.rgb, vColor.a * alpha);
		return;
	}
	outColor = texture(colorTexture, vUv) * vColor;
}
