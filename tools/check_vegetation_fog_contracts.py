#!/usr/bin/env python3
"""Guard vegetation-only local fog ownership, draw count and auxiliary ABI."""
from pathlib import Path
import re
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT/'code/rd-vulkan/vk_backend.cpp').read_text()
FRAG = (ROOT/'code/rd-vulkan/shaders/world.frag').read_text()


class VegetationFog(unittest.TestCase):
    def test_parent_membership_and_no_effect_weather_expansion(self):
        load = body(SOURCE,'VK_Backend_LoadWorld')
        self.assertIn('batch.fogIndex = LittleLong(surface.fogNum)',load)
        self.assertIn('surfaceSpriteFog && VK_IsVegetation(batch.stage.surfaceSprite)',load)
        self.assertIn('batch.stage.blendMode == VK_BLEND_ALPHA && batch.stage.depthWrite',load)
        self.assertNotIn('VK_ModelLocalFog(',load)
        weather = body(SOURCE,'VK_RecordWeather')
        self.assertNotIn('spriteFog',weather)
        build = body(SOURCE,'VK_WorldAppendSurfaceSpriteBatches')
        self.assertIn('batch.parentShader = shader;',build)
        self.assertNotIn('spriteFog',body(SOURCE,'VK_StreamSurfaceSpriteBatch'))

    def test_scope_and_single_draw(self):
        draw = body(SOURCE,'VK_RecordWorldSurfaceSprites')
        self.assertEqual(draw.count('vkCmdDraw('),1)
        self.assertIn('VK_LocalFogEnabled()',draw)
        self.assertIn('vk.vegetationFogCvar->integer != 0',draw)
        self.assertIn('if (boundFogOffset) VK_BindLightmaps();',draw)
        self.assertIn('std::make_tuple(batch.fogIndex, fog.eye, opaque)',draw)
        self.assertIn('!vk.worldViewFog.goggles',body(SOURCE,'VK_LocalFogEnabled'))
        for function in ('VK_DestroyWorldGeometry','VK_RecordTestPattern'):
            self.assertIn('vk.spriteFogFrameOffsets.clear();',body(SOURCE,function))
        self.assertIn('vk.lightmapNext++ * vk.lightmapStride',draw)
        self.assertNotIn('VK_UploadBuffer',draw)

    def test_auxiliary_abi_and_discard_order(self):
        struct = re.search(r'struct vk_lightmap_block_t\s*\{(.*?)\};',SOURCE,re.S)[1]
        for name in ('spriteFogPlane','spriteFogColorDepth','spriteFogEye'):
            self.assertIn(f'float {name}[4];',struct)
            self.assertIn(f'vec4 {name};',FRAG)
        self.assertIn('sizeof(vk_lightmap_block_t) == 176',SOURCE)
        self.assertIn('sizeof( vk_world_vertex_t ) == 56',SOURCE)
        self.assertIn('std::memset(mapped, 0, sizeof(vk_lightmap_block_t))',SOURCE)
        fog = FRAG.index('if (lm.spriteFogColorDepth.a > 0.0)')
        self.assertGreater(fog,FRAG.rindex('discard;'))
        self.assertGreater(fog,FRAG.index('outColor.a = fragmentAlpha;'))
        self.assertIn('localFogAmount(vViewDepth, pointDepth, lm.spriteFogEye.x',FRAG)


if __name__ == '__main__':
    unittest.main()
