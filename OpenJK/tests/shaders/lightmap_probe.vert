#version 450
layout(location=0) out vec4 vColor;
layout(location=1) out vec2 vUv;
layout(location=2) out float vViewDepth;
layout(location=3) out vec3 vPosition;
layout(location=4) out vec3 vNormal;
layout(location=5) out float vSpecularAlpha;
layout(push_constant) uniform ProbePush { mat4 controls; } pc;
void main()
{
	const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),
		vec2(-1,1),vec2(1,-1),vec2(1,1));
	gl_Position=vec4(corners[gl_VertexIndex],0.5,1);
	gl_Position.z += pc.controls[0][1];
	if (pc.controls[3][3] < -0.5)
		gl_Position = gl_VertexIndex % 3 == 0 ? pc.controls[0] :
			(gl_VertexIndex % 3 == 1 ? pc.controls[1] : pc.controls[2]);
	vColor=vec4(1); vUv=vec2(0.25,0.5); vViewDepth=1;
	if (pc.controls[0][3] == 77.0) vColor.a = pc.controls[0][2];
	if (pc.controls[0][0] > 0.5) vUv.x = corners[gl_VertexIndex].x * 0.5 + 0.5;
	vPosition=vec3(0); vNormal=vec3(0,0,1);
	vSpecularAlpha=0.25;
}
