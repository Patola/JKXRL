float localFogAmount(float distance, float pointDepth, float eyeDepth, float opaqueDepth)
{
	float fraction = 1.0;
	if (eyeDepth < 0.0)
	{
		if (pointDepth < 1.0) return 0.0;
		fraction = pointDepth / (pointDepth - eyeDepth);
	}
	else if (pointDepth < 0.0) return 0.0;
	// Legacy fogTable is a square-root ramp; evaluate it without a lookup texture.
	return sqrt(clamp(distance * fraction / max(opaqueDepth, 1.0), 0.0, 1.0));
}
