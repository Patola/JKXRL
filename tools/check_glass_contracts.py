#!/usr/bin/env python3
"""Guard the optional polygon query without changing the legacy quad path."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'


class GlassContracts(unittest.TestCase):
    def test_actual_jko_translation_and_glass_dispatch(self):
        common = (ROOT / 'code/cgame/cg_public.h').read_text()
        module = (ROOT / 'codeJK2/cgame/cg_public.h').read_text()
        client = (ROOT / 'code/client/cl_cgame.cpp').read_text()

        def enum(source, name):
            return next(match.group(0) for match in re.finditer(
                r'typedef enum\s*\{[^{}]*\}\s*(\w+)\s*;', source)
                if match.group(1) == name)

        common_enum = enum(common, 'cgameImport_t')
        mirror_enum = enum(common, 'cgameJK2Import_t')
        module_enum = enum(module, 'cgameImport_t')
        clean_enum = re.sub(r'/\*.*?\*/|//[^\n]*', '', module_enum, flags=re.S)
        names = re.findall(r'\bCG_[A-Z0-9_]+\b', clean_enum)
        assertions = '\n'.join(f'static_assert(int(module::{name}) == int({name}_JK2));'
                               for name in names)
        dispatch = body(client, 'CL_CgameSystemCalls')
        start = dispatch.index('case CG_R_GET_BMODEL_GLASS_POLYGON:')
        end = dispatch.index('case ', start+5)
        case = dispatch[start:end]
        harness = r'''
#include <cassert>
#include <cstdint>
using vec3_t = float[3];
''' + common_enum + mirror_enum + '\nnamespace module {\n' + module_enum + '\n}\n' + assertions + r'''
cgameImport_t CL_ConvertJK2SysCall(cgameJK2Import_t import) {
''' + body(client, 'CL_ConvertJK2SysCall') + r'''
}
int called=0;
float (*expectedVertices)[3], *expectedNormal;
int Query(int model, vec3_t* vertices, int capacity, vec3_t normal) {
    assert(model==197 && capacity==64);
    assert(vertices==expectedVertices && normal==expectedNormal);
    vertices[4][2]=123; normal[2]=1; ++called; return 5;
}
struct { int (*GetBModelGlassPolygon)(int,vec3_t*,int,float*)=Query; } re;
#define VMA(i) reinterpret_cast<void*>(args[i])
intptr_t Dispatch(intptr_t* args, bool jko) {
    if(jko) args[0]=CL_ConvertJK2SysCall(static_cast<cgameJK2Import_t>(args[0]));
    switch(args[0]) {
''' + case + r'''
    default: return -99;
    }
}
int main() {
    assert(CL_ConvertJK2SysCall(CG_HAPTICEVENT_JK2)==CG_HAPTICEVENT);
    assert(CL_ConvertJK2SysCall(CG_R_GET_BMODEL_GLASS_POLYGON_JK2)==CG_R_GET_BMODEL_GLASS_POLYGON);
    assert(int(CL_ConvertJK2SysCall(static_cast<cgameJK2Import_t>(9999)))==-1);
    float vertices[64][3]={}, normal[3]={};
    expectedVertices=vertices; expectedNormal=normal;
    for(int jko=0;jko<2;++jko) {
        intptr_t args[]={jko ? int(module::CG_R_GET_BMODEL_GLASS_POLYGON) : int(CG_R_GET_BMODEL_GLASS_POLYGON),
            197, reinterpret_cast<intptr_t>(vertices), 64, reinterpret_cast<intptr_t>(normal)};
        assert(Dispatch(args,jko)==5);
        assert(vertices[4][2]==123 && normal[2]==1);
    }
    assert(called==2);
    re.GetBModelGlassPolygon=nullptr;
    intptr_t args[]={module::CG_R_GET_BMODEL_GLASS_POLYGON,197,0,64,0};
    assert(Dispatch(args,true)==0 && called==2);
}
'''
        with tempfile.TemporaryDirectory(prefix='jkxr-glass-syscall-') as directory:
            executable = str(Path(directory) / 'probe')
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            '-x', 'c++', '-', '-o', executable], input=harness,
                           text=True, check=True, capture_output=True)
            subprocess.run([executable], check=True, capture_output=True)

    def test_matched_renderer_api_and_bounded_query(self):
        public = (ROOT / 'code/rd-common/tr_public.h').read_text()
        self.assertRegex(public, r'REF_API_VERSION\s+24\b')
        self.assertIn('(*GetBModelGlassPolygon)(int model, vec3_t *vertices, int capacity, vec3_t normal)', public)
        init = (ROOT / 'code/rd-vulkan/tr_init.cpp').read_text()
        self.assertIn('re.GetBModelGlassPolygon = VK_Backend_GetBModelGlassPolygon', init)
        backend = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()
        query = body(backend, 'VK_Backend_GetBModelGlassPolygon')
        for contract in ('capacity > GLASS_MAX_VERTICES', 'VectorClear(vertices[i])',
                         'vk.models.size()', 'VK_MODEL_INLINE_BSP', 'vk.world.inlineModels.size()',
                         'GlassSelectPolygon', 'vk.worldRefdef.viewaxis[0]'):
            self.assertIn(contract, query)
        client = body((ROOT / 'code/client/cl_cgame.cpp').read_text(), 'CL_CgameSystemCalls')
        self.assertIn('case CG_R_GET_BMODEL_GLASS_POLYGON:', client)
        self.assertIn('args[1], (float (*)[3])VMA(2), args[3], (float *)VMA(4)', client)

    def test_both_games_keep_quad_path_and_append_syscall(self):
        for game in ('code', 'codeJK2'):
            with self.subTest(game=game):
                public = (ROOT / game / 'cgame/cg_public.h').read_text()
                self.assertRegex(public, r'CG_HAPTICEVENT\s*,\s*CG_R_GET_BMODEL_GLASS_POLYGON')
                syscall = body((ROOT / game / 'cgame/cg_syscalls.cpp').read_text(), 'cgi_R_GetBModelGlassPolygon')
                self.assertIn('Q_syscall(CG_R_GET_BMODEL_GLASS_POLYGON, model, vertices, capacity, normal)', syscall)
                destroy = body((ROOT / game / 'game/g_breakable.cpp').read_text(), 'funcGlassDie')
                self.assertRegex(destroy, r'if \( GlassQuadValid\( verts \) \)\s*CG_DoGlass\(.*?;\s*else',)
                self.assertIn('if (!CG_DoGlassPolygon(', destroy)
                self.assertIn('self->contents = 0', destroy)
                self.assertIn('gi.AdjustAreaPortalState( self, qtrue )', destroy)
                effects = (ROOT / game / 'cgame/cg_effects.cpp').read_text()
                legacy = body(effects, 'CG_DoGlass')
                self.assertIn('CG_DoGlassShard( subVerts, biPoints, 4, stick, time, dmgDir )', legacy)
                polygon = body(effects, 'CG_DoGlassPolygon')
                self.assertIn('CG_DoGlassShard(shard.vertices, shard.uv, 3, stick, time, direction)', polygon)
                self.assertLess(polygon.index('if (shards.empty()) return false'), polygon.index('cgi_S_StartSound'))
                self.assertLess(polygon.index('cgi_S_StartSound'), polygon.index('for (auto &shard'))
                self.assertEqual(polygon.count('cgi_S_StartSound'), 1)


if __name__ == '__main__':
    unittest.main()
