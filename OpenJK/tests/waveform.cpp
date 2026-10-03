#include <boost/test/unit_test.hpp>
#include <limits>
#include "../code/rd-vulkan/vk_waveform.h"
#include "../code/rd-vulkan/vk_shader_animation.h"

BOOST_AUTO_TEST_SUITE(ShaderWaveform)
BOOST_AUTO_TEST_CASE(MaterialNoisePreservesAuthoredRangeAndStereoTime)
{
	const float brightness[]{.5f,.1f,0,25};
	const float stretch[]{1,.01f,0,.05f};
	float minimum=1, maximum=0;
	for (int frame=0;frame<1800;++frame)
	{
		const float time=frame/90.f;
		const float left=VK_EvaluateMaterialWave(VK_WAVE_NOISE,brightness,time,1);
		const float right=VK_EvaluateMaterialWave(VK_WAVE_NOISE,brightness,time,1);
		BOOST_CHECK_EQUAL(left,right);
		BOOST_CHECK_GE(left,.4f); BOOST_CHECK_LE(left,.6f);
		minimum=std::min(minimum,left); maximum=std::max(maximum,left);
		const float scale=VK_EvaluateMaterialWave(VK_WAVE_NOISE,stretch,time,1);
		BOOST_CHECK_GE(scale,.99f); BOOST_CHECK_LE(scale,1.01f);
	}
	BOOST_CHECK_GT(maximum-minimum,.15f);
	// The old unsupported noise path used unmodulated 1.0, not the authored 0.5.
	BOOST_CHECK_EQUAL(VK_EvaluateMaterialWave(VK_WAVE_NONE,brightness,0,1),1);
}
BOOST_AUTO_TEST_CASE(MaterialNoisePhaseContinuityAndInvalidInputs)
{
	const float parameters[]{.5f,.1f,.3f,4};
	BOOST_CHECK_CLOSE(VK_EvaluateMaterialWave(VK_WAVE_NOISE,parameters,2,1),
		.5f+.1f*vk_material_noise::Sample((2+.3f)*4),.0001f);
	for (float time : {-512.f,-256.f,-1.f,0.f,1.f,256.f,512.f})
	{
		BOOST_CHECK_SMALL(vk_material_noise::Sample(time-.0001f)-
			vk_material_noise::Sample(time+.0001f),.001f);
		BOOST_CHECK_EQUAL(vk_material_noise::Sample(time),vk_material_noise::Sample(time+256));
	}
	for (float time : {std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN()})
		BOOST_CHECK_EQUAL(VK_EvaluateMaterialWave(VK_WAVE_NOISE,parameters,time,.75f),.75f);
	const float bad[]{0,std::numeric_limits<float>::infinity(),0,1};
	BOOST_CHECK_EQUAL(VK_EvaluateMaterialWave(VK_WAVE_NOISE,bad,1,.75f),.75f);
	BOOST_CHECK(std::isfinite(vk_material_noise::Sample(std::numeric_limits<float>::max())));
	for (auto type : {VK_WAVE_SIN,VK_WAVE_TRIANGLE,VK_WAVE_SQUARE,VK_WAVE_SAWTOOTH,VK_WAVE_INVERSE_SAWTOOTH})
		BOOST_CHECK_EQUAL(VK_EvaluateMaterialWave(type,parameters,2,1),
			parameters[0]+parameters[1]*VK_EvaluateWaveform(type,parameters[2]+2*parameters[3]));
}
BOOST_AUTO_TEST_CASE(ExistingWaveShapesAndNegativePhases)
{
	BOOST_CHECK_SMALL(VK_EvaluateWaveform(VK_WAVE_SIN, 0), 0.00001f);
	BOOST_CHECK_CLOSE(VK_EvaluateWaveform(VK_WAVE_SIN, 0.25f), 1, 0.0001f);
	BOOST_CHECK_CLOSE(VK_EvaluateWaveform(VK_WAVE_SIN, -0.25f), -1, 0.0001f);
	BOOST_CHECK_EQUAL(VK_EvaluateWaveform(VK_WAVE_TRIANGLE, 0.25f), 1);
	BOOST_CHECK_EQUAL(VK_EvaluateWaveform(VK_WAVE_TRIANGLE, 0.75f), -1);
	BOOST_CHECK_EQUAL(VK_EvaluateWaveform(VK_WAVE_TRIANGLE, 1), 0);
	BOOST_CHECK_EQUAL(VK_EvaluateWaveform(VK_WAVE_SQUARE, 0.49f), 1);
	BOOST_CHECK_EQUAL(VK_EvaluateWaveform(VK_WAVE_SQUARE, 0.5f), -1);
	BOOST_CHECK_EQUAL(VK_EvaluateWaveform(VK_WAVE_SAWTOOTH, -0.25f), 0.75f);
	BOOST_CHECK_EQUAL(VK_EvaluateWaveform(VK_WAVE_INVERSE_SAWTOOTH, 0.25f), 0.75f);
}
BOOST_AUTO_TEST_CASE(ExplosionStagesCrossFadeAtThreeHertz)
{
	const float wave[4] = {0, 1, 0, 3};
	for (int frame = 0; frame < 900; ++frame)
	{
		const float seconds = frame / 90.0f;
		const float a = VK_EvaluateAlphaWave(VK_WAVE_SAWTOOTH, wave, seconds, 1);
		const float b = VK_EvaluateAlphaWave(VK_WAVE_INVERSE_SAWTOOTH, wave, seconds, 1);
		BOOST_CHECK_GE(a, 0); BOOST_CHECK_LE(a, 1);
		BOOST_CHECK_GE(b, 0); BOOST_CHECK_LE(b, 1);
		BOOST_CHECK_SMALL(a + b - 1, 2.01f / 255);
		BOOST_CHECK_EQUAL(a, VK_EvaluateAlphaWave(VK_WAVE_SAWTOOTH, wave, seconds, 1));
	}
}
BOOST_AUTO_TEST_CASE(CrystalAndSpiritAuthoredValues)
{
	const float crystal[4] = {0.3f, 0, 0, 0};
	const float crystal2[4] = {0.2f, 0, 0, 0};
	const float spirit[4] = {0.7f, 0.1f, 0.1f, 0.1f};
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_SIN, crystal, 1, 1), 76.0f / 255);
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_SIN, crystal2, 900, 1), 51.0f / 255);
	BOOST_CHECK_SMALL(VK_EvaluateAlphaWave(VK_WAVE_SIN, spirit, 1.5f, 1) - 0.8f, 1.01f / 255);
	BOOST_CHECK_SMALL(VK_EvaluateAlphaWave(VK_WAVE_SIN, spirit, 6.5f, 1) - 0.6f, 1.01f / 255);
}
BOOST_AUTO_TEST_CASE(DelayedExplosionsUseTheirOwnAnimationAge)
{
	const float wave[4] = {0, 1, 0, 3};
	const float time = 100.25f;
	const float ageA = time - 100.0f, ageB = time - 99.0f;
	BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(8, ageA, 3, true), 0);
	BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(8, ageB, 3, true), 3);
	BOOST_CHECK_EQUAL(VK_ShaderAnimationFrame(8, 10, 3, true), 7);
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_SAWTOOTH, wave, ageA, 1), 191.0f / 255);
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_INVERSE_SAWTOOTH, wave, ageA, 1), 63.0f / 255);
}
BOOST_AUTO_TEST_CASE(ClampsWavesButPreservesUnspecifiedOpacity)
{
	float wave[4] = {1, 0.5f, 0, 1};
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_NONE, wave, 0, 1.35f), 1.35f);
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_TRIANGLE, wave, 0.25f, 1), 1);
	wave[0] = 0;
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_TRIANGLE, wave, 0.75f, 1), 0);
	wave[1] = std::numeric_limits<float>::infinity();
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_SIN, wave, 0, 0.75f), 0.75f);
	wave[1] = 1;
	BOOST_CHECK_EQUAL(VK_EvaluateAlphaWave(VK_WAVE_SIN, wave,
		std::numeric_limits<float>::quiet_NaN(), 0.75f), 0.75f);
}
BOOST_AUTO_TEST_SUITE_END()
