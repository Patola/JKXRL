#!/usr/bin/env python3
"""Guard authored specular routing; production GLSL math also runs on Vulkan."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


class SpecularContracts(unittest.TestCase):
    def test_actual_parser_branch_and_generator_reset(self):
        parse = body(SOURCE, 'VK_ParseShaderFile')
        start = parse.index('stage.alphaWaveType = VK_WAVE_NONE;', parse.index('"alphaGen"'))
        end = parse.index('else if ( Q_stricmp( generator, "portal" )', start)
        fixture = r'''
#include <cstdlib>
#include <strings.h>
#include <cassert>
enum { VK_WAVE_NONE=0 };
const bool qtrue=true;
struct Stage { int alphaWaveType=3; bool specularAlpha=true; float alpha=1; } stage;
const char* tokens[2]; int cursor;
const char* COM_ParseExt(const char**, bool) { return tokens[cursor++]; }
int Q_stricmp(const char* a,const char* b) { return strcasecmp(a,b); }
void parse(const char* name,const char* value="") {
    tokens[0]=name; tokens[1]=value; cursor=0;
    const char* text=nullptr;
''' + parse[start:end] + r'''
}
int main() {
    parse("lightingSpecular"); assert(stage.specularAlpha && stage.alphaWaveType==0);
    parse("const","0.25"); assert(!stage.specularAlpha && stage.alpha==0.25f);
    parse("LIGHTINGSPECULAR"); assert(stage.specularAlpha);
    parse("vertex"); assert(!stage.specularAlpha);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp)/'parse.cpp', Path(tmp)/'parse'
            src.write_text(fixture)
            subprocess.run(['c++', '-std=c++17', str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_model_direction_is_scoped_and_diffuse_is_not_replaced(self):
        model = body(SOURCE, 'VK_RecordMD3ModelSurfaces')
        self.assertIn('VK_SetupEntityLighting(*entity, refdef, dynamicLights, &specularLighting)', model)
        self.assertIn('VK_SpecularLightScope specular(usesSpecular ? specularLighting.localDirection : nullptr)', model)
        self.assertIn('dedicatedDynamicLighting ? noDynamicLights : dynamicLights', model)
        inline = body(SOURCE, 'VK_RecordInlineModelSurfaces')
        self.assertIn('VK_SetupEntityLighting(entity, vk.worldRefdef, dynamicLights, &specularLighting)', inline)
        self.assertIn('~VK_SpecularLightScope() { vk.specularLight = previous; }', SOURCE)
        self.assertIn('std::make_pair(offset, vk.specularLight)', SOURCE)
        # Cache lifetime must match its frame-local UBO offsets.
        self.assertIn('vk.specularCache.clear()', body(SOURCE, 'VK_RecordTestPattern'))
        self.assertEqual(SOURCE.count('vk.specularCache.clear()'), 2)

    def test_only_authored_stages_and_no_extra_draws(self):
        push = body(SOURCE, 'VK_PushWorldStage')
        self.assertIn('stage->specularAlpha', push)
        self.assertIn('push.color[3] = push.alpha = 1.0f', push)
        self.assertNotIn('vkCmdDraw', push)
        self.assertIn('stage.specularAlpha = stageDefinition.specularAlpha', SOURCE)
        vert = (ROOT / 'code/rd-vulkan/shaders/world.vert').read_text()
        self.assertIn('pc.stageFlags.w < 10.0 && pc.stageFlags.x >= 4.0', vert)
        self.assertIn('inverse(pc.mvp)', vert)
        self.assertIn('deformNormal(inNormal)', vert)
        frag = (ROOT / 'code/rd-vulkan/shaders/world.frag').read_text()
        self.assertIn('if (specularAlpha) generatedColor.a = vSpecularAlpha', frag)
        self.assertIn('colorFlags - (alphaWave ? 2.0 : 0.0)', frag)


if __name__ == '__main__':
    unittest.main()
