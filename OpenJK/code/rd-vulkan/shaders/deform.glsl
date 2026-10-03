// Shared by color/fog/light and shadow/depth passes. std140 matches vk_deform_block_t.
layout(set = 1, binding = 0, std140) uniform DeformBlock
{
	vec4 control;
	vec4 meta[3];
	vec4 wave[3];
	vec4 vector[3];
	vec4 billboard[4];
	vec4 specularLight;
} deform;

float deformWave(int function, float phase)
{
	int index = int(mod(trunc(phase * 1024.0), 1024.0)) & 1023;
	float cycle = float(index) / 1024.0;
	if (function == 1) return sin(float(index) * (6.283185307179586 / 1023.0));
	if (function == 2) return cycle < 0.25 ? cycle * 4.0 : (cycle < 0.75 ? 2.0 - cycle * 4.0 : cycle * 4.0 - 4.0);
	if (function == 3) return cycle < 0.5 ? 1.0 : -1.0;
	if (function == 4) return cycle;
	if (function == 5) return 1.0 - cycle;
	return 0.0;
}

vec3 deformPosition(vec3 position, vec3 normal, float u)
{
	if(deform.control.w != 0.0)
	{
		vec4 p=vec4(position,1.0);
		position=vec3(dot(deform.billboard[0],p),dot(deform.billboard[1],p),dot(deform.billboard[2],p));
	}
	for (int i = 0; i < int(deform.control.x); ++i)
	{
		vec4 m = deform.meta[i], w = deform.wave[i], v = deform.vector[i];
		float value;
		if (m.x == 2.0)
			value = v.x == 0.0 && v.z == 0.0 ? v.y : v.y * deformWave(1,
				(u * v.x + deform.control.z * v.z) / 6.283185307179586);
		else
		{
			float offset = m.x == 1.0 && w.w != 0.0 ? (position.x + position.y + position.z) * m.z : 0.0;
			value = w.x + w.y * deformWave(int(m.y), w.z + deform.control.y * w.w + offset);
		}
		position += value * (m.x == 3.0 ? v.xyz : normal);
	}
	return position;
}

vec3 deformNormal(vec3 normal)
{
	return deform.control.w != 0.0 ? deform.billboard[3].xyz : normal;
}
