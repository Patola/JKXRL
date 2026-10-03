#version 450

layout(location = 0) out vec3 vColor;

layout(push_constant) uniform DiagnosticPush
{
	mat4 mvp;
} pc;

const vec3 cubeCorners[8] = vec3[](
	vec3(-1.0, -1.0, -1.0),
	vec3( 1.0, -1.0, -1.0),
	vec3( 1.0,  1.0, -1.0),
	vec3(-1.0,  1.0, -1.0),
	vec3(-1.0, -1.0,  1.0),
	vec3( 1.0, -1.0,  1.0),
	vec3( 1.0,  1.0,  1.0),
	vec3(-1.0,  1.0,  1.0)
);

const int cubeIndices[36] = int[](
	0, 1, 2, 0, 2, 3,
	4, 6, 5, 4, 7, 6,
	0, 4, 5, 0, 5, 1,
	1, 5, 6, 1, 6, 2,
	2, 6, 7, 2, 7, 3,
	3, 7, 4, 3, 4, 0
);

void main()
{
	int bar = gl_VertexIndex / 36;
	int corner = cubeIndices[gl_VertexIndex % 36];

	vec3 center = vec3(0.0);
	vec3 halfSize = vec3(5.0);
	vec3 color = vec3(1.0);

	if (bar == 0)
	{
		center = vec3(62.0, 0.0, 0.0);
		halfSize = vec3(62.0, 5.5, 5.5);
		color = vec3(1.0, 0.10, 0.08);
	}
	else if (bar == 1)
	{
		center = vec3(0.0, 0.0, 0.0);
		halfSize = vec3(5.5, 70.0, 5.5);
		color = vec3(0.10, 1.0, 0.26);
	}
	else
	{
		center = vec3(0.0, 0.0, 45.0);
		halfSize = vec3(5.5, 5.5, 45.0);
		color = vec3(0.20, 0.55, 1.0);
	}

	vec3 localPosition = center + cubeCorners[corner] * halfSize;
	vColor = color;
	gl_Position = pc.mvp * vec4(localPosition, 1.0);
}
