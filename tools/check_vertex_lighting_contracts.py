#!/usr/bin/env python3
"""Execute the legacy color combiner and the renderer's sparse update path."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
SOURCE = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()


def execute(fixture):
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'test.cpp'
        src.write_text(fixture)
        exe = Path(tmp) / 'test'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-I',
                        str(ROOT / 'code/rd-vulkan'), str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class VertexLighting(unittest.TestCase):
    def test_actual_legacy_math(self):
        legacy = (ROOT / 'code/rd-vanilla/tr_surface.cpp').read_text()
        execute(r'''
#include "vk_vertex_lighting.h"
#include <cassert>
#include <cstring>
#include <random>
using byte=uint8_t;
union byteAlias_t { byte b[4]; uint32_t ui; };
constexpr int LIGHTMAP_BY_VERTEX=-3, MAXLIGHTMAPS=4, LS_UNUSED=254;
struct Shader { int lightmapIndex[4]={-3}; byte styles[4]; } shader;
struct { Shader* shader; } tess{&shader};
struct Cvar { int integer=0; } fullbright, *r_fullbright=&fullbright;
byte styleColors[64][4];
unsigned Com_Clamp(unsigned lo,unsigned hi,unsigned v) { return std::clamp(v,lo,hi); }
uint32_t legacy(const byte* colors) {
''' + body(legacy, 'ComputeFinalVertexColor') + r'''
}
int main() {
    using namespace vk_vertex_lighting;
    std::mt19937 rng(27);
    for (int test=0; test<20000; ++test) {
        Colors colors; Palette palette; Styles styles;
        for (auto& slot:colors) for(auto& c:slot) c=rng();
        for (auto& slot:palette) for(auto& c:slot) c=rng();
        const int slots=1+test%4;
        styles.fill(254);
        for(int s=0;s<slots;++s) styles[s]=rng()%64;
        std::copy(styles.begin(),styles.end(),shader.styles);
        std::memcpy(styleColors,palette.data(),sizeof(styleColors));
        const auto got=Combine(colors,styles,palette);
        const auto expected=legacy(colors[0].data());
        assert(std::memcmp(got.data(),&expected,4)==0);
        assert(got[3]==colors[0][3]);
    }
    Colors colors{}; for(auto& c:colors) c={255,255,255,19};
    Palette palette{}; for(auto& c:palette) c={255,255,255,255};
    assert((Combine(colors,{0,254,254,254},palette)==Color{254,254,254,19}));
    assert((Combine(colors,{0,1,2,3},palette)==Color{255,255,255,19}));
    assert((Combine(colors,{0,254,1,2},palette)==Color{254,254,254,19}));
    auto other=palette; other[9][0]=0;
    assert(!Changed({0,1,254,254},palette,other));
    other[1][0]=0; assert(Changed({0,1,254,254},palette,other));
}
''')

    def test_actual_sparse_update_and_restore(self):
        execute(r'''
#include "vk_vertex_lighting.h"
#include <vulkan/vulkan.h>
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdarg>
struct vk_world_vertex_t { float position[3], color[4], uv[2], lightmapUv[2], normal[3]; };
static_assert(sizeof(vk_world_vertex_t)==56);
struct Lighting { uint32_t firstVertex; vk_vertex_lighting::Styles styles;
    std::vector<vk_vertex_lighting::Colors> colors; std::vector<vk_world_vertex_t> vertices; };
struct World { std::vector<Lighting> vertexLighting; VkBuffer vertexBuffer=(VkBuffer)1;
    int vertexLightingMode=-1; vk_vertex_lighting::Palette vertexLightStyles{}; };
struct Cvar { int integer=1; } enabled;
struct { World world; Cvar* vertexStylesCvar=&enabled; vk_vertex_lighting::Palette lightStyles{};
    VkCommandBuffer commandBuffer{}; } vk;
constexpr int PRINT_ALL=0;
struct { void Printf(int,const char*,...) {} } ri;
std::vector<vk_world_vertex_t> gpu;
size_t packets=0, barriers=0;
extern "C" void vkCmdUpdateBuffer(VkCommandBuffer,VkBuffer,VkDeviceSize off,VkDeviceSize bytes,const void* src) {
    assert(off%4==0 && bytes%4==0 && bytes>0 && bytes<=65536);
    assert(off+bytes<=gpu.size()*sizeof(vk_world_vertex_t));
    std::memcpy(reinterpret_cast<char*>(gpu.data())+off,src,bytes); ++packets;
}
extern "C" void vkCmdPipelineBarrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,
    VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t count,const VkBufferMemoryBarrier* b,
    uint32_t,const VkImageMemoryBarrier*) {
    assert(count==1 && b->buffer==vk.world.vertexBuffer); ++barriers;
}
void update() {
''' + body(SOURCE, 'VK_UpdateWorldVertexLighting') + r'''
}
int main() {
    gpu.resize(3020);
    for(size_t i=0;i<gpu.size();++i) {
        gpu[i].position[0]=float(i); gpu[i].uv[0]=0.321f;
        gpu[i].color[0]=0.123f; gpu[i].color[3]=0.456f;
    }
    const auto original=gpu;
    Lighting block{}; block.firstVertex=10; block.styles={0,1,254,254};
    block.vertices.assign(gpu.begin()+10,gpu.begin()+3010);
    block.colors.resize(3000);
    for(auto& c:block.colors) { c[0]={50,20,10,116}; c[1]={100,90,80,222}; }
    vk.world.vertexLighting.push_back(block);
    vk.lightStyles[0]={255,255,255,255}; vk.lightStyles[1]={255,255,255,255};
    update(); assert(packets==3 && barriers==2 && vk.world.vertexLightingMode==1);
    for(size_t i=0;i<gpu.size();++i) {
        if(i<10 || i>=3010) assert(std::memcmp(&gpu[i],&original[i],sizeof(gpu[i]))==0);
        else {
            assert(gpu[i].color[0]==149/255.0f);
            auto v=gpu[i]; std::copy_n(original[i].color,3,v.color);
            assert(std::memcmp(&v,&original[i],sizeof(v))==0);
        }
    }
    update(); assert(packets==3 && barriers==2);
    vk.lightStyles[9][0]=123; update(); assert(packets==3);
    vk.lightStyles[1][0]=0; update(); assert(packets==6 && gpu[10].color[0]==49/255.0f);
    enabled.integer=0; update(); assert(packets==9);
    assert(std::memcmp(gpu.data(),original.data(),gpu.size()*sizeof(gpu[0]))==0);
    vk.lightStyles[1][0]=255; update(); assert(packets==9);
    enabled.integer=1; update(); assert(packets==12 && gpu[10].color[0]==149/255.0f);
    vk.world.vertexLightingMode=-1; update(); assert(packets==15); // Aborted-frame retry.
    vk.world.vertexLighting.clear(); update(); assert(packets==15);
}
''')

    def test_stereo_and_material_contracts(self):
        record = body(SOURCE, 'VK_RecordTestPattern')
        self.assertIn('if (eye == 0 && vk.sceneWorldRenderedThisFrame) VK_UpdateWorldVertexLighting();', record)
        self.assertLess(record.index('VK_UpdateWorldVertexLighting();'), record.index('VK_RecordShadowMap('))
        self.assertIn('if (!ready) vk.world.vertexLightingMode = -1;', body(SOURCE, 'VK_RenderEyes'))
        self.assertIn('usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT', body(SOURCE, 'VK_UploadBuffer'))
        material = body(SOURCE, 'VK_RecordBoundIndexedShader')
        self.assertEqual(material.count('combinedVertexStyles && vk.world.vertexLightingMode == 1'), 2)
        self.assertIn('validLighting', SOURCE)
        self.assertIn('sampled.size() != lighting.colors.size()', SOURCE)


if __name__ == '__main__':
    unittest.main()
