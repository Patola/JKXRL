#!/usr/bin/env python3
"""Guard lightstyle/overlap integration; math and actual GLSL have separate tests."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


class LightmapContracts(unittest.TestCase):
    def test_inverse_alpha_parser_and_pipeline_contract(self):
        parser = body(SOURCE, 'VK_ParseShaderFile')
        start = parser.index('const std::string source =')
        end = parser.index('else if ( Q_stricmp( token, "alphaGen" )')
        branch = parser[start:end].rstrip().removesuffix('}')
        fixture = '''
#include <string>
#include <strings.h>
#include <cassert>
#include "vk_material_blend.h"
const bool qtrue=true;
const char *tokens[2]; int cursor;
const char* COM_ParseExt(const char**, bool) { return tokens[cursor++]; }
int Q_stricmp(const char* a,const char* b) { return strcasecmp(a,b); }
vk_blend_mode_t parse(const char* a,const char* b) {
    tokens[0]=a; tokens[1]=b; cursor=0;
    const char* text=nullptr;
    struct { vk_blend_mode_t blendMode=VK_BLEND_OPAQUE; } stage;
'''+branch+'''
    return stage.blendMode;
}
int main() {
    assert(parse("GL_ONE_MINUS_SRC_ALPHA","GL_SRC_ALPHA")==VK_BLEND_INVERSE_ALPHA);
    assert(parse("gl_one_minus_src_alpha","gl_one_minus_src_alpha")==VK_BLEND_INVERSE_ALPHA_BOTH);
    assert(parse("GL_ONE_MINUS_SRC_ALPHA","GL_ONE")==VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE);
    assert(parse("GL_SRC_ALPHA","GL_ONE_MINUS_SRC_ALPHA")==VK_BLEND_ALPHA);
    assert(parse("GL_SRC_ALPHA","GL_ONE")==VK_BLEND_SOURCE_ALPHA_ADDITIVE);
    assert(parse("filter","")==VK_BLEND_MODULATE);
    assert(parse("GL_ONE","GL_ZERO")==VK_BLEND_OPAQUE);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)/'blend.cpp'
            src.write_text(fixture)
            exe = Path(tmp)/'blend'
            subprocess.run(['c++', '-std=c++17', '-I', str(ROOT/'code/rd-vulkan'),
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        for field in ('worldInverseAlphaPipeline', 'worldInverseAlphaBothPipeline',
                      'texturedRectInverseAlphaPipeline', 'texturedRectInverseAlphaBothPipeline'):
            self.assertIn(field+' = VK_NULL_HANDLE;', SOURCE)
            self.assertIn('&vk.'+field, body(SOURCE, 'VK_CreatePipelines'))
            self.assertIn('vkDestroyPipeline( vk.device, vk.'+field+', nullptr )', SOURCE)
        for field in ('worldInverseAlphaPipeline','worldInverseAlphaBothPipeline'):
            self.assertIn('return vk.'+field, body(SOURCE,'VK_WorldPipelineForBlend'))

    def test_decal_order_uses_actual_pass_classifier_and_preserves_other_maps(self):
        enums = '\n'.join(re.search(r'enum ' + name + r'\s*\{[^}]+\};', SOURCE)[0]
                          for name in ('vk_world_pass_t', 'vk_surface_sprite_type_t'))
        fixture = '''
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include "vk_material_blend.h"
using qhandle_t=int;
'''+enums+'''
struct vk_material_stage_t {
    struct { vk_surface_sprite_type_t type=VK_SURFACE_SPRITE_NONE; } surfaceSprite;
    bool lightmap=false;
    vk_blend_mode_t blendMode=VK_BLEND_OPAQUE;
};
struct vk_material_t { bool polygonOffset=false; std::vector<vk_material_stage_t> stages; };
struct vk_world_batch_t { int shader; bool vertexLit=false; };
struct vk_world_geometry_t { std::vector<vk_world_batch_t> batches; };
struct { std::vector<vk_material_t> materials; } vk;
bool VK_HasViewFog() { return false; }
bool VK_ShaderUsesPass(qhandle_t shader, bool vertexLit, vk_world_pass_t pass) {
'''+body(SOURCE, 'VK_ShaderUsesPass')+'''
}
void order(const vk_world_geometry_t& world, std::vector<uint32_t>& order) {
'''+body(SOURCE, 'VK_OrderDecalBatches')+'''
}
int main() {
    vk.materials.resize(6);
    vk.materials[1].polygonOffset=true;
    vk.materials[1].stages.resize(1);
    vk.materials[1].stages[0].blendMode=VK_BLEND_ALPHA;
    vk.materials[2].stages.resize(2);
    vk.materials[2].stages[0].lightmap=true;
    vk.materials[2].stages[1].blendMode=VK_BLEND_MODULATE;
    vk.materials[3].stages.resize(1);
    vk.materials[3].stages[0].blendMode=VK_BLEND_ALPHA;
    vk.materials[4]=vk.materials[2]; // Solid cutout, not a decal.
    vk.materials[4].polygonOffset=true;
    vk_world_geometry_t world{{{1},{3},{2},{1},{2},{4},{0}}};
    std::vector<uint32_t> ids{0,1,2,3,4,5,6};
    order(world,ids);
    assert((ids==std::vector<uint32_t>{2,4,5,6,0,3,1}));
    auto once=ids; order(world,ids); assert(ids==once);
    ids={1,2,4,5,6}; once=ids; order(world,ids); assert(ids==once);
    ids={3,4,0}; order(world,ids); // An inline BSP model's subset.
    assert((ids==std::vector<uint32_t>{4,3,0}));
    ids.clear(); order(world,ids); assert(ids.empty());
    world.batches[0]={2,true}; // Vertex-lit promotion must match actual drawing.
    ids={3,0}; order(world,ids); assert((ids==std::vector<uint32_t>{0,3}));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)/'decal.cpp'
            src.write_text(fixture)
            exe = Path(tmp)/'decal'
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            '-I', str(ROOT/'code/rd-vulkan'), str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_decal_order_only_changes_bsp_translucent_submission(self):
        for name in ('VK_RecordWorld', 'VK_RecordInlineModelSurfaces'):
            draw = body(SOURCE, name)
            self.assertIn('orderedDecals = pass == VK_WORLD_PASS_TRANSLUCENT', draw)
            self.assertIn('translucentBatchOrder[i]', draw)
        load = body(SOURCE, 'VK_Backend_LoadWorld')
        self.assertIn('VK_WorldPrepareDecalOrder(world)', load)
        prepare = body(SOURCE, 'VK_WorldPrepareDecalOrder')
        self.assertIn('VK_WorldBatchBelongsToRoot', prepare)
        self.assertIn('surfaceBatchIndex', prepare)
        self.assertEqual(prepare.count('VK_OrderDecalBatches'), 2)
        for token in ('vkCmd', 'depthWrite', 'alpha', 'cull', 'vertices', 'indices'):
            self.assertNotIn(token, prepare)

    def test_flat_cutout_plane_culling_is_scoped(self):
        load = body(SOURCE, 'VK_WorldLoadDeformFaces')
        for token in ('MST_PATCH', 'material.depthMaskedLightmap && material.deforms.empty()',
                      'VK_MATERIAL_TWO_SIDED', 'std::fabs(d) <= 0.001f',
                      'if (!flat) continue', 'face.flatCutout = maskedPatch',
                      'vertices[firstVertex].normal[axis]'):
            self.assertIn(token, load)
        visibility = body(SOURCE, 'VK_FilterWorldDeformFaces')
        self.assertIn('refdef.vieworg', visibility)
        self.assertIn('face.extent, face.flatCutout', visibility)

    def test_rock_probe_and_boundary_join_are_scoped(self):
        self.assertNotIn('VK_WorldCloseRiftRockSeams', SOURCE)
        self.assertNotIn('vk_planar_seam', SOURCE)
        load = body(SOURCE, 'VK_Backend_LoadWorld')
        for token in ('"maps/t3_rift.bsp"', '"textures/rift/rock3_phong"',
                      'VK_WorldBatchBelongsToRoot', 'material.stages.size() == 2',
                      '!material.stages[0].lightmap && material.stages[1].lightmap'):
            self.assertIn(token, load)
        self.assertLess(load.index('VK_WorldJoinRiftBoundaries('), load.index('VK_UploadBuffer('))
        join = body(SOURCE, 'VK_WorldJoinRiftBoundaries')
        for token in ('!batch.riftSeamProbe', 'MST_PLANAR', 'vk_rock_boundary::Collect',
                      'vk_rock_boundary::Find', 'vk_rock_boundary::Apply', 'visited[index]'):
            self.assertIn(token, join)
        self.assertIn('left.riftSeamProbe == right.riftSeamProbe', body(SOURCE, 'VK_WorldIndirectBatchesMatch'))
        draw = body(SOURCE, 'VK_RecordBoundIndexedShader')
        for token in ('VK_RiftSeamProbeMode(worldBatch)', 'riftProbe == 4',
                      'pass != VK_WORLD_PASS_OPAQUE', 'stage.blendMode = VK_BLEND_OPAQUE',
                      'VK_SetWorldDepthBias(false)', 'riftProbe == 3 ? lightmap : stage.texture'):
            self.assertIn(token, draw)
        self.assertIn('VK_RiftSeamProbeMode(worldBatch)', body(SOURCE, 'VK_RecordBoundIndexedFog'))
        self.assertIn('VK_RiftSeamProbeMode(&batch)', body(SOURCE, 'VK_RecordWorldDynamicLights'))
        self.assertIn('"r_vulkanRiftSeamDebug", "0", 0', SOURCE)

    def test_probe_default_and_exclusion(self):
        fixture = '''
#include <algorithm>
#include <cassert>
struct vk_world_batch_t { bool riftSeamProbe=false; };
struct Cvar { int integer; };
struct State { Cvar* riftSeamDebugCvar=nullptr; } vk;
int VK_ClampValue(int n,int a,int b) { return std::clamp(n,a,b); }
int probe(const vk_world_batch_t* batch) {
'''+body(SOURCE, 'VK_RiftSeamProbeMode')+'''
}
int main() {
    vk_world_batch_t eligible{true}, ordinary{};
    assert(probe(&eligible)==0);
    Cvar c{}; vk.riftSeamDebugCvar=&c;
    for(int n=-1;n<=5;++n) {
        c.integer=n;
        assert(probe(nullptr)==0);
        assert(probe(&ordinary)==0);
        assert(probe(&eligible)==std::clamp(n,0,4));
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)/'probe.cpp'
            src.write_text(fixture)
            exe = Path(tmp)/'probe'
            subprocess.run(['c++','-std=c++17',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_actual_cutout_classifier_accepts_parser_alpha_fallback(self):
        enums = '\n'.join(re.search(r'enum ' + name + r'\s*\{[^}]+\};', SOURCE)[0]
                          for name in ('vk_alpha_test_t', 'vk_depth_func_t', 'vk_surface_sprite_type_t'))
        fixture = '''
#include <vector>
#include <cstddef>
#include "vk_material_blend.h"
'''+enums+'''
struct vk_material_stage_t {
    struct { vk_surface_sprite_type_t type = VK_SURFACE_SPRITE_NONE; } surfaceSprite;
    bool lightmap = false, depthWrite = false;
    int texture = 7;
    vk_alpha_test_t alphaTest = VK_ALPHA_TEST_NONE;
    vk_depth_func_t depthFunc = VK_DEPTH_FUNC_LEQUAL;
    vk_blend_mode_t blendMode = VK_BLEND_OPAQUE;
};
bool match(const std::vector<vk_material_stage_t>& stages) {
'''+body(SOURCE, 'VK_IsDepthMaskedLightmap')+'''
}
int main() {
    std::vector<vk_material_stage_t> s(3);
    s[0].alphaTest=VK_ALPHA_TEST_GREATER_EQUAL_HALF;
    s[0].depthWrite=true;
    s[0].blendMode=VK_BLEND_ALPHA; // Actual SRC_ALPHA/ZERO parser fallback.
    s[1].lightmap=true;
    s[1].depthFunc=s[2].depthFunc=VK_DEPTH_FUNC_EQUAL;
    s[2].blendMode=VK_BLEND_MODULATE;
    if (!match(s)) return 1;
    s[0].blendMode=VK_BLEND_OPAQUE;
    if (!match(s)) return 2;
    for(int i=0;i<6;++i) {
        auto bad=s;
        if(i==0) bad[0].depthWrite=false;
        if(i==1) bad[0].alphaTest=VK_ALPHA_TEST_NONE;
        if(i==2) bad[1].depthFunc=VK_DEPTH_FUNC_LEQUAL;
        if(i==3) bad[2].texture=8;
        if(i==4) bad[0].blendMode=VK_BLEND_ADDITIVE;
        if(i==5) bad[2].surfaceSprite.type=VK_SURFACE_SPRITE_VERTICAL;
        if(match(bad)) return 3+i;
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'cutout.cpp'
            src.write_text(fixture)
            exe = Path(tmp) / 'cutout'
            subprocess.run(['c++', '-std=c++17', '-I', str(ROOT / 'code/rd-vulkan'),
                            str(src), '-o', str(exe)], check=True, capture_output=True)
            subprocess.run([str(exe)], check=True)

    def test_cutout_finishing_passes_are_scoped_and_depth_equal(self):
        matcher = body(SOURCE, 'VK_IsDepthMaskedLightmap')
        for token in ('stages.size() != 3', 'mask.alphaTest != VK_ALPHA_TEST_NONE',
                      'mask.depthWrite', 'light.lightmap', 'color.texture == mask.texture',
                      'light.depthFunc == VK_DEPTH_FUNC_EQUAL',
                      'color.depthFunc == VK_DEPTH_FUNC_EQUAL',
                      'light.blendMode == VK_BLEND_OPAQUE',
                      'color.blendMode == VK_BLEND_MODULATE'):
            self.assertIn(token, matcher)
        draw = body(SOURCE, 'VK_RecordBoundIndexedShader')
        self.assertIn('worldBatch != nullptr && !vertexLit &&', draw)
        self.assertIn('material.depthMaskedLightmap && effectiveStage.depthFunc == VK_DEPTH_FUNC_EQUAL', draw)
        self.assertIn('maskedEqual && stagePipelineOverride == VK_NULL_HANDLE', draw)
        register = body(SOURCE, 'VK_Backend_RegisterTexture')
        self.assertIn('VK_IsDepthMaskedLightmap( vk.materials[handle].stages )', register)
        self.assertRegex(register, r'if \( vk.materials\[handle\].depthMaskedLightmap \)\s*\{\s*'
                         r'(?://[^\n]*\n\s*)*vk.materials\[handle\].stages.front\(\).blendMode = VK_BLEND_OPAQUE;')
        for field in ('worldMaskedLightmapEqualPipeline', 'worldMaskedModulateEqualPipeline'):
            self.assertIn(f'{field} = VK_NULL_HANDLE;', SOURCE)
            self.assertIn(f'vkDestroyPipeline( vk.device, vk.{field}, nullptr )', SOURCE)
            self.assertRegex(SOURCE, '&vk.' + field + r', [^;]+?true, false, true,\s*'
                             r'VK_CULL_MODE_NONE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_COMPARE_OP_EQUAL')

    def test_compatible_layouts_and_resource_lifetime(self):
        for name in ('VK_CreatePipeline', 'VK_CreateShadowMapPipeline'):
            self.assertIn('vk.lightmapSetLayout', body(SOURCE, name))
        self.assertEqual(SOURCE.count('{vk.textureSetLayout, vk.deformSetLayout, vk.lightmapSetLayout}'), 2)
        reset = body(SOURCE, 'VK_RecordTestPattern')
        self.assertIn('vk.lightmapFrameOffsets.clear()', reset)
        self.assertIn('vk.lightmapNext = 1', reset)
        load = body(SOURCE, 'VK_Backend_LoadWorld')
        self.assertLess(load.index('vkDeviceWaitIdle'), load.index('vkResetDescriptorPool'))

    def test_material_draws_and_ordinary_fallback(self):
        draw = body(SOURCE, 'VK_RecordBoundIndexedShader')
        self.assertEqual(draw.count('VK_LightmapScope lightmaps'), 3)
        self.assertIn('!fullbrightWorld', draw)
        self.assertIn('VK_BindLightmaps()', body(SOURCE, 'VK_BindDeformOffset'))
        self.assertIn('VK_WorldTextureUsable', body(SOURCE, 'VK_BindLightmaps'))

    def test_coplanar_fix_changes_grouping_not_geometry(self):
        isolate = body(SOURCE, 'VK_WorldIsolateCoplanarPasses')
        for token in ('VK_WorldBatchBelongsToRoot', 'stages.empty()', 'MST_PLANAR', 'VK_TrianglesOverlap'):
            self.assertIn(token, isolate)
        for token in ('vkCmd', 'polygonOffset', 'indices.erase', 'vertices.erase'):
            self.assertNotIn(token, isolate)
        self.assertIn('isolatedMaterialPasses', body(SOURCE, 'VK_WorldIndirectBatchesMatch'))

    def test_camera_uses_saved_entity_origins_in_both_games(self):
        for game in ('code', 'codeJK2'):
            source = (ROOT / game / 'cgame/cg_ents.cpp').read_text()
            general = body(source, 'CG_General')
            self.assertIn('VR_HideActiveCameraPart', general)
            self.assertIn('cent->gent->currentOrigin, camera->currentOrigin', general)
            self.assertIn('"misc_camera"', general)

    def check_water_order(self, source):
        world = body(source, 'VK_RecordWorld')
        self.assertRegex(world, r'if \( pass == VK_WORLD_PASS_TRANSLUCENT &&\s*'
                         r'\(VK_ShaderIsYavinWaterOverlay\( batch.shader \) \|\|\s*'
                         r'VK_ShaderIsFogSurfaceOverlay\( batch.shader \)\) \)\s*'
                         r'\{\s*continue;')
        self.assertNotIn('stageMajorShaders', world)
        self.assertRegex(world, r'if \( !deferWaterOverlay \)\s*\{\s*beginPhase\(\);\s*'
                         r'VK_RecordWorldWaterOverlay\( eye \);')
        ordered = [world.index(token) for token in (
            'recordWorldPass( VK_WORLD_PASS_TRANSLUCENT )',
            'recordWorldPass( VK_WORLD_PASS_FOG )',
            'VK_RecordWorldWaterOverlay( eye )',
            'VK_RecordBlendedMD3s( view, projection )')]
        self.assertEqual(ordered, sorted(ordered))
        late = body(source, 'VK_RecordWorldLateEffects')
        self.assertLess(late.index('VK_RecordWorldWaterOverlay( eye, true )'),
                        late.index('VK_RecordBlendedMD3s'))

    def test_water_composition_independent_of_shadow_casters(self):
        self.check_water_order(SOURCE)
        overlay = body(SOURCE, 'VK_RecordWorldWaterOverlay')
        self.assertLess(overlay.index('for ( size_t stageIndex'),
                        overlay.index('for ( const vk_world_batch_t &riverBatch'))
        self.assertIn('VK_WORLD_PASS_TRANSLUCENT', overlay)
        self.assertIn('visibleSurfaces', overlay)
        self.assertNotIn('vkCmdClearAttachments', overlay)

    def test_reject_water_interleaved_without_shadows(self):
        broken = SOURCE.replace('if ( pass == VK_WORLD_PASS_TRANSLUCENT &&\n'
                                '\t\t\t\t (VK_ShaderIsYavinWaterOverlay',
                                'if ( deferWaterOverlay && pass == VK_WORLD_PASS_TRANSLUCENT &&\n'
                                '\t\t\t\t (VK_ShaderIsYavinWaterOverlay', 1)
        self.assertNotEqual(broken, SOURCE)
        with self.assertRaises(AssertionError):
            self.check_water_order(broken)


if __name__ == '__main__':
    unittest.main()
