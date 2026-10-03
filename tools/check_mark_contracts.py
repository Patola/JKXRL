#!/usr/bin/env python3
"""Source wiring guards for the CPU projected-mark API and its drawing path."""
from pathlib import Path
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


class MarkContracts(unittest.TestCase):
    def test_dynamic_uv_transforms_reach_both_color_and_glow(self):
        self.assertIn('stage.tcTransforms = stageDefinition.tcTransforms;', SOURCE)
        stream = body(SOURCE, 'VK_StreamDynamicEffectBatch')
        self.assertIn('VK_TransformTextureCoordinate( vertices[i].uv, transform )', stream)
        for name in ('VK_RecordDynamicEffectBatch', 'VK_RecordDynamicGlowBatch'):
            draw = body(SOURCE, name)
            self.assertIn('&stage.tcTransforms', draw)
            self.assertIn('&stageOffset', draw)

    def test_emplaced_motor_stops_for_death_and_normal_exit(self):
        source = (ROOT / 'code/game/g_emplaced.cpp').read_text()
        self.assertIn('ent->owner->s.loopSound = 0;', body(source, 'ExitEmplacedWeapon'))
        self.assertIn('self->s.loopSound = 0;', body(source, 'eweb_die'))

    def test_real_export_is_cpu_only_and_not_camera_dependent(self):
        exports = (ROOT / 'code/rd-vulkan/tr_init.cpp').read_text()
        self.assertIn('re.MarkFragments = VK_Backend_MarkFragments;', exports)
        query = body(SOURCE, 'VK_Backend_MarkFragments')
        for forbidden in ('vkCmd', 'VK_Upload', 'visibleSurfaces', 'viewCount', 'worldRefdef', 'ri.CM_Trace'):
            self.assertNotIn(forbidden, query)
        for required in ('markVisited', 'markQuerySerial', 'world.nodes', 'world.leafSurfaces',
                         'projector.Clip', 'vk_marks::WriteFragment', 'candidateCount<64'):
            self.assertIn(required, query)

    def test_cache_obeys_bsp_material_flags_and_static_world_ownership(self):
        load = body(SOURCE, 'VK_Backend_LoadWorld')
        self.assertIn('SURF_NOIMPACT | SURF_NOMARKS', load)
        self.assertIn('contentFlags) & CONTENTS_FOG', load)
        self.assertIn('s - staticWorld.firstSurface >= staticWorld.surfaceCount', load)
        self.assertIn('reversePatchNormal', load)
        destroy = body(SOURCE, 'VK_DestroyWorldGeometry')
        for member in ('markPositions', 'markIndices', 'markSurfaces', 'markVisited'):
            self.assertIn(f'vk.world.{member}.clear()', destroy)

    def test_effect_batches_apply_and_reset_authored_depth_bias(self):
        draw = body(SOURCE, 'VK_RecordDynamicEffectBatch')
        self.assertIn('VK_SetWorldDepthBias', draw)
        self.assertIn('vk.materials[batch.shader].polygonOffset', draw)
        self.assertLess(draw.index('VK_SetWorldDepthBias'), draw.index('VK_BindWorldPipeline'))
        for tree in ('code', 'codeJK2'):
            marks = (ROOT / tree / 'cgame/cg_marks.cpp').read_text()
            self.assertIn('cgi_CM_MarkFragments', marks)
            self.assertIn('CG_AllocMark', marks)
            self.assertIn('CG_AddMarks', marks)


if __name__ == '__main__':
    unittest.main()
