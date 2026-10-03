// Copyright (C) 2026 JKXRL contributors. GPL-2.0-or-later.
#pragma once
#include <algorithm>
#include <cmath>
#include "vk_material_noise.h"

enum vk_waveform_t
{
	VK_WAVE_NONE,
	VK_WAVE_SIN,
	VK_WAVE_TRIANGLE,
	VK_WAVE_SQUARE,
	VK_WAVE_SAWTOOTH,
	VK_WAVE_INVERSE_SAWTOOTH,
	VK_WAVE_NOISE,
};

inline float VK_EvaluateWaveform(vk_waveform_t type, float value)
{
	const float cycle = value - std::floor(value);
	switch (type)
	{
	case VK_WAVE_SIN: return std::sin(cycle * 6.28318530717958647692f);
	case VK_WAVE_TRIANGLE:
		if (cycle < 0.25f) return cycle * 4.0f;
		if (cycle < 0.75f) return 2.0f - cycle * 4.0f;
		return cycle * 4.0f - 4.0f;
	case VK_WAVE_SQUARE: return cycle < 0.5f ? 1.0f : -1.0f;
	case VK_WAVE_SAWTOOTH: return cycle;
	case VK_WAVE_INVERSE_SAWTOOTH: return 1.0f - cycle;
	default: return 0.0f;
	}
}

inline float VK_EvaluateMaterialWave(vk_waveform_t type, const float wave[4],
	float seconds, float fallback)
{
	if (type == VK_WAVE_NONE || !std::isfinite(seconds)) return fallback;
	for (int i=0;i<4;++i) if (!std::isfinite(wave[i])) return fallback;
	// Legacy noise phases are time offsets, unlike periodic waveform phases.
	const float phase = type == VK_WAVE_NOISE
		? (seconds+wave[2])*wave[3] : wave[2]+seconds*wave[3];
	if (!std::isfinite(phase)) return fallback;
	const float sample = type == VK_WAVE_NOISE
		? vk_material_noise::Sample(phase) : VK_EvaluateWaveform(type,phase);
	const float result=wave[0]+wave[1]*sample;
	return std::isfinite(result) ? result : fallback;
}

inline float VK_EvaluateAlphaWave(vk_waveform_t type, const float wave[4],
	float seconds, float fallback)
{
	if (type == VK_WAVE_NONE || !std::isfinite(seconds)) return fallback;
	for (int i = 0; i < 4; ++i)
		if (!std::isfinite(wave[i])) return fallback;
	const float phase = wave[2] + seconds * wave[3];
	if (!std::isfinite(phase)) return fallback;
	const float value = wave[0] + wave[1] * VK_EvaluateWaveform(type, phase);
	if (!std::isfinite(value)) return fallback;
	// RB_CalcWaveAlpha replaces generated alpha and stores a clamped byte.
	return std::floor(std::clamp(value, 0.0f, 1.0f) * 255.0f) / 255.0f;
}
