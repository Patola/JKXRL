#!/usr/bin/env python3
"""Source wiring guards; weather math is tested in the C++ unit suite."""
from pathlib import Path
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
BACKEND = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


class WeatherContracts(unittest.TestCase):
    def test_new_weather_presets_reuse_stereo_exposure_and_separate_blending(self):
        build = body(BACKEND, 'VK_BuildWeatherBatches')
        self.assertIn('if (vk.weatherBatchFrame == vk.frameIndex) return;', build)
        self.assertIn('settings.Additive() ? VK_BLEND_ADDITIVE : VK_BLEND_ALPHA', build)
        self.assertIn('batch.stage.depthWrite = false;', build)
        self.assertIn('settings.opacity', build)
        self.assertIn('!VK_Backend_IsOutside(instance.position)', build)
        self.assertIn('settings.HorizontalSpan()', build)
        self.assertIn('settings.VerticalSpan()', build)
        self.assertIn('settings.FadeEnd()', build)
        self.assertIn('instance.color[3] = settings.Additive() ? 1.0f : envelope;', build)
        self.assertIn('std::stable_sort(batch.instances.begin()', build)
        self.assertIn('vk.worldRefdef.viewaxis[0]', build)
        record = body(BACKEND, 'VK_RecordWeather')
        self.assertIn('VK_BindWorldPipeline(batch.stage.blendMode, &boundPipeline)', record)
        prepare = body(BACKEND, 'VK_PrepareWeatherResources')
        self.assertIn('settings.FallbackTexture()', prepare)
        self.assertIn('VK_FindOrLoadImage(settings.Texture())', prepare)
        self.assertIn('layer.shader = VK_FindOrLoadImage(texture);', prepare)
        reset = body(BACKEND, 'VK_ResetWorldEffects')
        self.assertIn('vk.weatherLayers = {};', reset)
        self.assertIn('vk.weatherDrawLayers = {};', reset)

    def test_weather_console_command_lifetime_and_forwarding(self):
        exports = (ROOT / 'code/rd-vulkan/tr_init.cpp').read_text()
        command = body(exports, 'R_WorldEffect_f')
        self.assertIn('ri.Cmd_ArgsBuffer(command, sizeof(command));', command)
        self.assertIn('VK_Backend_WorldEffectCommand(command);', command)
        self.assertIn('ri.Cmd_AddCommand("r_we", R_WorldEffect_f);',
                      body(exports, 'RE_BeginRegistration'))
        shutdown = exports[exports.index('re.Shutdown ='):exports.index('re.BeginRegistration =')]
        self.assertIn('ri.Cmd_RemoveCommand("r_we");', shutdown)

    def test_exports_use_live_backend(self):
        exports = (ROOT / 'code/rd-vulkan/tr_init.cpp').read_text()
        self.assertIn('re.GetChanceOfSaberFizz = VK_Backend_GetChanceOfSaberFizz;', exports)
        self.assertIn('re.SetTempGlobalFogColor = VK_Backend_SetTempGlobalFogColor;', exports)
        self.assertIn('vk.weatherLayers.SaberFizzChance()',
                      body(BACKEND, 'VK_Backend_GetChanceOfSaberFizz'))

    def test_fog_surfaces_and_background_share_override(self):
        self.assertIn('vk.weatherFogFlash.Color(vk.world.globalFogColor)',
                      body(BACKEND, 'VK_CurrentViewFog'))
        for function in ('VK_PushWorldFogStage', 'VK_RecordTestPattern'):
            source = body(BACKEND, function)
            self.assertIn('VK_CurrentViewFog()', source)
            self.assertNotRegex(source, r'=\s*vk\.world\.globalFogColor\[')
        setter = body(BACKEND, 'VK_Backend_SetTempGlobalFogColor')
        self.assertIn('vk.weatherFogFlash.Set(color, vk.world.hasGlobalFog)', setter)
        self.assertNotIn('vkCmd', setter)
        for function in ('VK_ResetWorldEffects', 'VK_DestroyWorldGeometry'):
            self.assertIn('vk.weatherFogFlash = {}', body(BACKEND, function))

    def test_fizz_retains_gameplay_exposure_and_stock_effect(self):
        jka = (ROOT / 'code/game/wp_saber.cpp').read_text()
        start = jka.index('float chanceOfFizz = gi.WE_GetChanceOfSaberFizz()')
        self.assertIn('!g_saberNoEffects && gi.WE_IsOutside', jka[start - 160:start])
        self.assertIn('G_PlayEffect( "saber/fizz", end )', jka[start:start + 650])
        # JKO uses an older independent rain path; don't silently replace it.
        jko = (ROOT / 'codeJK2/game/wp_saber.cpp').read_text()
        self.assertIn('level.worldFlags&WF_RAINING', jko)
        self.assertIn('G_PlayEffect( "saber/fizz", end, normal )', jko)


if __name__ == '__main__':
    unittest.main()
