#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform ShadowMapPush
{
	mat4 lightMvp;
} pc;

void main()
{
	gl_Position = pc.lightMvp * vec4(inPosition, 1.0);
}
