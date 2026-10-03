// Legacy RB_CalcSpecularAlpha: byte-quantized per-vertex Phong exponent 4.
vec3 specularUnit(vec3 v)
{
	return v * inversesqrt(max(dot(v, v), 1.0e-20));
}

float materialSpecularAlpha(vec3 position, vec3 normal, vec3 eye, vec4 modelLight)
{
	vec3 light = modelLight.w > 0.5 ? modelLight.xyz
		: specularUnit(vec3(-960.0, 1980.0, 96.0) - position);
	vec3 reflected = normal * (2.0 * dot(normal, light)) - light;
	float l = max(dot(reflected, specularUnit(eye - position)), 0.0);
	l *= l;
	l *= l;
	return floor(clamp(l, 0.0, 1.0) * 255.0) / 255.0;
}
