#!/usr/bin/env python3
"""Guard authored alpha-wave isolation, coverage and effect-relative clocks."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


class AlphaWaveContracts(unittest.TestCase):
    def test_material_noise_matches_legacy_interpolation_without_game_rng(self):
        legacy = (ROOT / 'code/rd-common/tr_noise.cpp').read_text()
        fixture = '''
#include "vk_waveform.h"
#include <cassert>
#include <cstdlib>
#define NOISE_MASK 255
#define VAL(a) s_noise_perm[(a)&NOISE_MASK]
#define INDEX(x,y,z,t) VAL(x+VAL(y+VAL(z+VAL(t))))
#define LERP(a,b,w) (a*(1.0f-w)+b*w)
float s_noise_table[256];
int s_noise_perm[256];
float GetNoiseValue(int x,int y,int z,int t) {
''' + body(legacy, 'GetNoiseValue') + '''
}
float reference(float x,float y,float z,float t) {
''' + body(legacy, 'R_NoiseGet4f') + '''
}
int main() {
    std::srand(42); const int expected=std::rand();
    std::srand(42); vk_material_noise::Sample(0);
    assert(std::rand()==expected);
    const vk_material_noise::Tables table;
    for(int i=0;i<256;++i) {
        s_noise_table[i]=table.values[i]; s_noise_perm[i]=table.permutation[i];
    }
    for(int i=-8192;i<=8192;++i) {
        const float t=i/16.f;
        assert(std::fabs(reference(0,0,0,t)-vk_material_noise::Sample(t))<.00001f);
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            '-I', str(ROOT / 'code/rd-vulkan'), str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_noise_is_only_enabled_for_material_color_and_stretch(self):
        parse = body(SOURCE, 'VK_ParseShaderFile')
        for field in ('rgbWaveType', 'stretchType'):
            self.assertIn('stage.' + field + ' = VK_ParseMaterialWaveform(', parse)
        self.assertNotIn('VK_WAVE_NOISE', body(SOURCE, 'VK_ParseWaveform'))
        self.assertNotIn('VK_ParseMaterialWaveform', body(SOURCE, 'VK_ParseDeform'))
        push = body(SOURCE, 'VK_PushWorldStage')
        self.assertIn('VK_EvaluateMaterialWave(stage->rgbWaveType, stage->rgbWave, seconds, 1)', push)
        self.assertIn('VK_EvaluateMaterialWave(stage->stretchType, stage->stretch, seconds, 1)', push)
        self.assertIn('std::floor(std::min(wave,1.0f)*255.0f)/255.0f', push)

    def test_shell_depth_precedes_color_and_reuses_skinned_geometry(self):
        model = body(SOURCE, 'VK_RecordMD3ModelSurfaces')
        self.assertIn('depthAlphaGLMCull && !disintegrating', model)
        self.assertIn('vk.materials[shader].cull != VK_MATERIAL_TWO_SIDED', model)
        self.assertIn('shellDraws.push_back( { shader, vertexBuffer, vertexOffset, indexBuffer, indexCount } )', model)
        shell = model[model.index('const auto recordShellPass'):]
        self.assertNotIn('VK_StreamSkinnedGLMSurface', shell)
        self.assertIn('depthOnly ? 0 : -1', shell)
        self.assertLess(shell.index('recordShellPass( true )'), shell.index('recordShellPass( false )'))
        self.assertIn('depthOnly ? vk.depthAlphaGLMPrepassPipelines[material.cull]', shell)
        self.assertIn('if ( !colorWrite ) colorAttachment.colorWriteMask = 0', body(SOURCE, 'VK_CreatePipeline'))
        self.assertIn('VK_COMPARE_OP_LESS_OR_EQUAL, false', body(SOURCE, 'VK_CreatePipelines'))
        self.assertIn('for ( VkPipeline &pipeline : vk.depthAlphaGLMPrepassPipelines )', SOURCE)

    def test_depth_alpha_glm_preserves_authored_cull_and_depth_together(self):
        model = body(SOURCE, 'VK_RecordMD3ModelSurfaces')
        self.assertIn('VK_DepthAlphaGLMCull( model.type == VK_MODEL_GLM', model)
        self.assertIn('first.alphaWaveType != VK_WAVE_NONE', model)
        self.assertIn(': -1, authoredMD3Cull, depthAlphaGLMCull', model)
        draw = body(SOURCE, 'VK_RecordBoundIndexedShader')
        self.assertIn('effectiveStage.depthWrite && blendMode == VK_BLEND_ALPHA', draw)
        self.assertIn('? vk.depthAlphaGLMPipelines[material.cull]', draw)
        create = body(SOURCE, 'VK_CreatePipelines')
        self.assertIn('VK_BLEND_ALPHA, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true', create)
        self.assertIn('&vk.depthAlphaGLMPipelines[side]', create)
        self.assertIn('for ( VkPipeline &pipeline : vk.depthAlphaGLMPipelines )', SOURCE)

    def test_parser_retains_valid_waves_without_consuming_braces(self):
        parser = body(SOURCE, 'VK_ParseShaderFile')
        self.assertIn('stage.alphaWaveType = VK_WAVE_NONE', parser)
        self.assertIn('if (valid) stage.alphaWaveType = type', parser)
        self.assertIn('std::isfinite(parameter)', parser)
        self.assertIn('text = valueStart; valid = false; break;', parser)
        self.assertIn('stage.alphaWaveType = stageDefinition.alphaWaveType', SOURCE)
        self.assertIn('std::memcpy(stage.alphaWave, stageDefinition.alphaWave', SOURCE)

    def test_color_and_fog_preserve_rgb_but_replace_generated_alpha(self):
        push = body(SOURCE, 'VK_PushWorldStage')
        self.assertIn('VK_EvaluateAlphaWave(stage->alphaWaveType', push)
        self.assertIn('if (stage->alphaWaveType != VK_WAVE_NONE)', push)
        self.assertIn('push.flags[0] += 2.0f', push)
        self.assertIn('push.color[3] = 1.0f', push)
        self.assertIn('maskStage->alphaWaveType != VK_WAVE_NONE', body(SOURCE, 'VK_PushWorldFogStage'))
        shader = (ROOT / 'code/rd-vulkan/shaders/world.frag').read_text()
        self.assertIn('if (alphaWave) generatedColor.a = 1.0;', shader)
        self.assertIn('mix(vec4(1.0), vColor, vertexColorWeight)', shader)
        self.assertIn('coverage = alphaWave ? pc.stageColor.a : vColor.a', shader)
        self.assertIn('fogAmount * textureCoverage * coverage', shader)
        self.assertIn('localFog && pc.stageFlags.z < 0.5 ? 1.0 : texel.a', shader)
        ui = body(SOURCE, 'VK_Backend_DrawPic')
        self.assertIn('color[3] = stage.alphaWaveType != VK_WAVE_NONE', ui)
        self.assertIn(': stage.color[3] * vk.currentColor[3] * stage.alpha', ui)

    def test_only_alpha_wave_effects_split_by_start_time(self):
        build = body(SOURCE, 'VK_BuildDynamicEffectBatches')
        self.assertIn('vk.materials[shader].hasAlphaWave', build)
        self.assertIn('timedAlpha && std::isfinite(entity.shaderTime)', build)
        batch = body(SOURCE, 'VK_DynamicEffectBatchForShader')
        self.assertIn('batch.shader == shader && batch.shaderTime == shaderTime', batch)
        texture = body(SOURCE, 'VK_DynamicEffectStageTexture')
        self.assertIn('vk.materials[batch.shader].hasAlphaWave', texture)
        self.assertIn('stage.oneShotAnimation', texture)
        self.assertIn('- batch.shaderTime', texture)
        for name in ('VK_RecordDynamicEffectBatch', 'VK_RecordDynamicGlowBatch'):
            draw = body(SOURCE, name)
            self.assertIn('VK_DynamicEffectStageTexture(batch, stage)', draw)
            self.assertIn('nullptr, false, batch.shaderTime', draw)


if __name__ == '__main__':
    unittest.main()
