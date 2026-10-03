#!/usr/bin/env python3
"""View-effect lifetime and shader wiring; numerical contracts live in C++ tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


class ViewFogContracts(unittest.TestCase):
    def test_fog_liquid_classifier(self):
        fixture = '''
#include <algorithm>
#include <vector>
#include <cassert>
constexpr int VK_SURFACE_SPRITE_NONE=0, VK_BLEND_OPAQUE=0;
struct Sprite { int type=0; };
struct vk_material_stage_t { Sprite surfaceSprite; int blendMode=1; bool depthWrite=false; };
bool classify(bool hasFog, bool seeThroughSort, const std::vector<vk_material_stage_t>& stages) {
''' + body(SOURCE, 'VK_IsFogSurfaceOverlay') + '''
}
int main() {
    std::vector<vk_material_stage_t> stages(3);
    assert(classify(true,true,stages));
    assert(!classify(false,true,stages));
    assert(!classify(true,false,stages));
    assert(!classify(true,true,{}));
    stages[0].depthWrite=true; assert(!classify(true,true,stages));
    stages[0].depthWrite=false; stages[0].blendMode=0;
    assert(!classify(true,true,stages));
    stages[0].blendMode=1; stages[0].surfaceSprite.type=1;
    assert(!classify(true,true,stages));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_fog_liquid_finishes_after_terrain_fog(self):
        world = body(SOURCE, 'VK_RecordWorld')
        self.assertIn('VK_ShaderIsFogSurfaceOverlay( batch.shader )', world)
        self.assertLess(world.index('recordWorldPass( VK_WORLD_PASS_FOG )'),
                        world.index('VK_RecordWorldWaterOverlay( eye )'))
        self.assertIn('VK_ShaderIsFogSurfaceOverlay( batch.shader )',
                      body(SOURCE, 'VK_RecordWorldWaterOverlay'))
        fog = body(SOURCE, 'VK_RecordBoundIndexedFog')
        self.assertIn('local.depth > 0 && worldBatch && VK_ShaderIsFogSurfaceOverlay(shader)', fog)
        self.assertIn('worldFogSurfaceEqualPipeline', fog)

    def test_local_fog_keeps_raw_slots_and_batch_boundaries(self):
        load = body(SOURCE, 'VK_WorldLoadLocalFog')
        self.assertIn('world.localFogs.resize(fogsView.count)', load)
        self.assertIn('if (brush==-1) continue', load)
        for lump in ('FOGS', 'BRUSHES', 'BRUSHSIDES', 'PLANES'):
            self.assertIn('LUMP_' + lump, load)
        self.assertIn('static_cast<size_t>(plane)>=planesView.count', load)
        self.assertIn('sidesView.count-static_cast<size_t>(first)', load)
        self.assertIn('batch.fogIndex = LittleLong(surfaces[batch.surfaceIndex].fogNum)',
                      body(SOURCE, 'VK_Backend_LoadWorld'))
        self.assertIn('left.fogIndex == right.fogIndex',
                      body(SOURCE, 'VK_WorldIndirectBatchesMatch'))
        destroy = body(SOURCE, 'VK_DestroyWorldGeometry')
        self.assertIn('vk.world.localFogs.clear()', destroy)
        self.assertIn('vk.world.localFogCount = 0', destroy)

    def test_local_fog_models_and_goggles(self):
        self.assertIn('!vk.worldViewFog.goggles', body(SOURCE, 'VK_LocalFogEnabled'))
        model = body(SOURCE, 'VK_ModelLocalFog')
        self.assertIn('VK_BuildEntityModelMatrix(*entity,matrix)', model)
        self.assertIn('refdef.vieworg,matrix)', model)
        for function in ('VK_RecordInlineModelSurfaces', 'VK_RecordMD3ModelSurfaces'):
            self.assertIn('VK_ModelLocalFog(', body(SOURCE, function))
            self.assertIn('&localFog', body(SOURCE, function))
        draw = body(SOURCE, 'VK_RecordBoundIndexedFog')
        self.assertIn('if (modelFog) local = *modelFog;', draw)
        self.assertIn('local.depth > 0 ? &local : nullptr', draw)

    def test_local_plane_parameters_do_not_become_uvs_or_lightmap_controls(self):
        vertex = (ROOT / 'code/rd-vulkan/shaders/world.vert').read_text()
        fragment = (ROOT / 'code/rd-vulkan/shaders/world.frag').read_text()
        self.assertIn('localFog ? inUv', vertex)
        self.assertEqual(fragment.count('!localFog && pc.useLightmap > 0.5'), 2)
        self.assertIn('localFogAmount(vViewDepth, pointDepth, pc.stageFlags.y, pc.alpha)', fragment)
        self.assertIn('pc.stageFlags.w - (localFog ? 15.0 : 10.0)', fragment)

    def test_binocular_projection_is_distinct_from_rifle_artwork_and_goggles(self):
        draw = body(SOURCE, 'VK_Backend_DrawPic')
        self.assertIn('VK_TextureHandleHasName(shader, "gfx/2d/binMask")', draw)
        self.assertIn('vk.binocularZoomThisFrame = true', draw)
        self.assertIn('vk.binocularZoomThisFrame = false', body(SOURCE, 'VK_Backend_BeginFrame'))
        active = body(SOURCE, 'VK_OpticalZoomActive')
        self.assertIn('VK_DisruptorScopeActive() || vk.binocularZoomThisFrame', active)
        self.assertNotIn('goggles', active)
        projection = body(SOURCE, 'VK_WorldProjectionTangentScales')
        self.assertIn('VK_OpticalZoomTangentScale()', projection)
        self.assertIn('*scaleX = scopeScale', projection)
        self.assertIn('*scaleY = scopeScale', projection)
        self.assertIn('VK_OpticalZoomActive() || !vk.worldRefdef.override_fov', projection)

    def test_live_exports_and_scene_scoped_goggles(self):
        exports = (ROOT / 'code/rd-vulkan/tr_init.cpp').read_text()
        self.assertIn('re.LAGoggles = VK_Backend_LAGoggles;', exports)
        self.assertIn('re.SetRangedFog = VK_Backend_SetRangedFog;', exports)
        self.assertIn('pendingViewFog.ClearScene()', body(SOURCE, 'VK_Backend_ClearScene'))
        self.assertIn('pendingViewFog.BeginFrame()', body(SOURCE, 'VK_Backend_BeginFrame'))
        self.assertIn('vk.pendingViewFog = {}', body(SOURCE, 'VK_DestroyWorldGeometry'))
        render = body(SOURCE, 'VK_Backend_RenderScene')
        self.assertIn('vk.worldViewFog = viewFog', render)
        self.assertIn('vk.portalViewFog = viewFog', render)
        self.assertEqual(body(SOURCE, 'VK_RecordSubmittedWorld').count(
            'std::swap( vk.worldViewFog, vk.portalViewFog )'), 2)

    def test_only_baked_rgb_is_overridden(self):
        push = body(SOURCE, 'VK_PushWorldStage')
        self.assertIn('push.padding = useLightmap ? 1.0f : 2.0f', push)
        draw = body(SOURCE, 'VK_RecordBoundIndexedShader')
        self.assertIn('worldBatch != nullptr && vk.worldViewFog.goggles', draw)
        lighting = body(SOURCE, 'VK_SetupEntityLighting')
        self.assertIn('RDF_NOWORLDMODEL | RDF_doLAGoggles', lighting)
        shader = (ROOT / 'code/rd-vulkan/shaders/world.frag').read_text()
        self.assertIn('texel.rgb = vec3(1.0)', shader)
        self.assertIn('generatedColor.rgb = vec3(1.0)', shader)
        self.assertIn('(vViewDepth - pc.padding) / max(pc.alpha - pc.padding, 1.0)', shader)

    def test_game_retains_battery_gate_and_jka_scope_formula(self):
        for tree in ('code', 'codeJK2'):
            draw = (ROOT / tree / 'cgame/cg_draw.cpp').read_text()
            self.assertIn('cg.zoomMode == 3 && cg.snap->ps.batteryCharge', draw)
            self.assertIn('cgi_R_LAGoggles();', draw)
        view = (ROOT / 'code/cgame/cg_view.cpp').read_text()
        self.assertIn('cgi_R_SetRangeFog(cg.refdef.fov_x*64.0f)', view)
        self.assertIn('cgi_R_SetRangeFog(0.0f)', view)


if __name__ == '__main__':
    unittest.main()
