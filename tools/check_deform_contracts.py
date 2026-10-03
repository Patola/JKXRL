#!/usr/bin/env python3
"""Source integration guards; numerical behavior is tested in C++ and on Vulkan."""
from pathlib import Path
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


class DeformContracts(unittest.TestCase):
    def test_bsp_flares_load_once_and_use_existing_stereo_effect_stream(self):
        load = body(SOURCE, 'VK_Backend_LoadWorld')
        for token in ('!= MST_FLARE', 'lightmapOrigin[axis]', 'lightmapVecs[2][axis]',
                      'world.flares.push_back', 'root.surfaceCount', 'inline flare'):
            self.assertIn(token, load)
        append = body(SOURCE, 'VK_AppendWorldFlares')
        for token in ('vk.flaresCvar->integer', 'VK_WorldVisibleSurfaceMask',
                      'refdef.viewaxis[1]', 'refdef.viewaxis[2]', 'vk_flare::Build',
                      'flare.surfaceIndex', 'poly.shader = flare.shader'):
            self.assertIn(token, append)
        for token in ('vkCmd', 'vkQueue', 'ReadPixels', 'RegisterTexture', 'VK_FindOrLoadImage'):
            self.assertNotIn(token, append)
        scene = body(SOURCE, 'VK_Backend_RenderScene')
        self.assertEqual(scene.count('VK_AppendWorldFlares('), 2)
        self.assertIn('VK_AppendWorldFlares(*refdef, vk.portalPolys)', scene)
        self.assertIn('VK_AppendWorldFlares(*refdef, vk.worldPolys)', scene)
        self.assertIn('vk.world.flares.clear()', body(SOURCE, 'VK_DestroyWorldGeometry'))
        self.assertIn('for ( const vk_scene_poly_t &poly : polys )', body(SOURCE, 'VK_BuildDynamicEffectBatches'))
        draw = body(SOURCE, 'VK_RecordDynamicEffectBatch')
        self.assertIn('VK_BindWorldPipeline( stage.blendMode', draw)
        self.assertNotIn('VK_DEPTH_FUNC_DISABLED', draw)
        self.assertRegex(SOURCE, r'&vk.worldAdditivePipeline,[^\n]+\n\s*'
                         r'VK_BLEND_ADDITIVE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true')

    def test_billboard_scope_and_shared_eye_parameters(self):
        loader = body(SOURCE, 'VK_WorldLoadBillboards')
        for token in ('VK_WorldBatchBelongsToRoot', 'batch.indexCount != 6',
                      '!material.deforms.empty()', 'Frame(quad', 'unsupported quad',
                      'world.billboardQuads.push_back', 'batch.mins[a]', 'batch.maxs[a]'):
            self.assertIn(token, loader)
        self.assertNotIn('lightmapUv', loader)
        self.assertIn('if (quad.mode == 1)', loader)
        load = body(SOURCE, 'VK_Backend_LoadWorld')
        self.assertLess(load.index('VK_WorldLoadBillboards'), load.index('VK_UploadBuffer'))
        self.assertIn('vk.billboardCache.clear()', body(SOURCE, 'VK_RecordTestPattern'))
        self.assertIn('vk.worldRefdef.viewaxis[a][c]', SOURCE)
        self.assertIn('left.billboard >= 0 || right.billboard >= 0',
                      body(SOURCE, 'VK_WorldIndirectBatchesMatch'))
        for name in ('VK_RecordBoundIndexedShader', 'VK_RecordBoundIndexedFog'):
            self.assertIn('VK_DeformScope deformation(shader, vk.deformEntityTime, worldBatch)', body(SOURCE, name))
        self.assertIn('VK_DeformScope deformation(batch.shader, vk.deformEntityTime, &batch)',
                      body(SOURCE, 'VK_RecordWorldDynamicLights'))
        self.assertIn('groupDrawCount, false, &batch', body(SOURCE, 'VK_RecordWorld'))
        self.assertIn('VK_NULL_HANDLE, 0, 0, false, &batch', body(SOURCE, 'VK_RecordWorld'))

    def test_color_and_depth_share_evaluator(self):
        for name in ('world.vert', 'shadow_map.vert'):
            shader = (ROOT / 'code/rd-vulkan/shaders' / name).read_text()
            self.assertIn('#include "deform.glsl"', shader)
            self.assertIn('deformPosition(inPosition, inNormal, inUv.x)', shader)
        shadow = body(SOURCE, 'VK_CreateShadowMapPipeline')
        self.assertIn('VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT', shadow)
        self.assertIn('sizeof( float ) * 32', shadow)
        self.assertIn('{vk.textureSetLayout, vk.deformSetLayout, vk.lightmapSetLayout}', shadow)
        for name in ('VK_RecordShadowMap', 'VK_RecordLightShadowReceiverMask'):
            self.assertIn('VK_DeformScope deformation(shader,', body(SOURCE, name))

    def test_material_draw_coverage(self):
        for name in ('VK_RecordBoundIndexedShader', 'VK_RecordBoundIndexedFog',
                     'VK_RecordMD3ModelSurfaces', 'VK_RecordInlineModelSurfaces',
                     'VK_RecordWorldDynamicLights', 'VK_RecordDynamicEffectBatch',
                     'VK_RecordDynamicGlowBatch'):
            self.assertIn('VK_DeformScope deformation(', body(SOURCE, name), name)

    def test_clock_and_descriptor_boundaries(self):
        for name in ('VK_RecordMD3ModelSurfaces', 'VK_RecordDynamicEffects'):
            self.assertIn('refdef.time)', body(SOURCE, name))
        for name in ('VK_RecordTestPattern', 'VK_RecordWorld', 'VK_RecordWorldLateEffects',
                     'VK_RecordLightShadowReceiverMask', 'VK_RecordDynamicEffects'):
            self.assertIn('VK_BindDeformOffset(0)', body(SOURCE, name))
        frame = body(SOURCE, 'VK_RecordTestPattern')
        self.assertIn('vk.ghoul2CacheFrameIndex != vk.frameIndex', frame)
        self.assertIn('vk.deformCache.clear()', frame)
        self.assertIn('vk.deformNext = 1', frame)
        self.assertIn('std::make_tuple(shader, time, shaderTime)', SOURCE)
        self.assertIn('!vk.materials[shader].deforms.empty()', body(SOURCE, 'VK_BuildDynamicEffectBatches'))

    def test_bounds_expand_without_changing_model_origin(self):
        self.assertIn('VK_MaterialDeformExtent(shader)', body(SOURCE, 'VK_WorldAppendBatch'))
        self.assertIn('inlineModel.deformExtent', body(SOURCE, 'VK_InlineModelIntersectsView'))
        for name in ('VK_ModelEntityIntersectsView', 'VK_ShadowCasterRadius',
                     'VK_RecordMD3ModelSurfaces'):
            self.assertIn('VK_EntityDeformExtent(', body(SOURCE, name))
        self.assertIn('model.mins[axis] - deformExtent', body(SOURCE, 'VK_DynamicLightIntersectsModel'))

    def test_parser_is_bounded_and_preserves_braces(self):
        parser = body(SOURCE, 'VK_ParseDeform')
        self.assertIn('definition.deforms.size() < VK_MAX_DEFORMS', parser)
        self.assertIn('std::isfinite(value)', parser)
        self.assertIn('if (!valid) return 0.0f', parser)
        self.assertIn('*text = before', parser)
        self.assertNotIn('SkipRestOfLine', parser)
        self.assertIn('unsupported %s', parser)
        self.assertNotIn('VK_DEFORM_AUTOSPRITE', parser)

    def test_deforming_planar_faces_share_visibility_without_global_culling(self):
        loader = body(SOURCE, 'VK_WorldLoadDeformFaces')
        for token in ('staticWorld.firstSurface', 'staticWorld.surfaceCount',
                      '!= MST_PLANAR', 'material.deforms.empty()',
                      'material.cull == VK_MATERIAL_TWO_SIDED',
                      'lightmapVecs[2][axis]'):
            self.assertIn(token, loader)
        select = body(SOURCE, 'VK_FilterWorldDeformFaces')
        self.assertIn('DotProduct(refdef.vieworg, face.normal)', select)
        self.assertIn('VK_DeformFaceVisible', select)
        self.assertNotIn('vkCmd', select)
        visible = body(SOURCE, 'VK_WorldVisibleSurfaceMask')
        self.assertEqual(visible.count('return VK_FilterWorldDeformFaces'), 4)
        indirect = body(SOURCE, 'VK_UpdateWorldIndirectVisibility')
        self.assertIn('( *visibleSurfaces )[batch.surfaceIndex] != 0', indirect)


if __name__ == '__main__':
    unittest.main()
