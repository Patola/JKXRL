#!/usr/bin/env python3
"""Run the production Rift boundary loader against read-only retail BSP data."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-base', type=Path, required=True)
    args = parser.parse_args()
    data = None
    for path in sorted(args.game_base.glob('*.pk3')):
        with zipfile.ZipFile(path) as archive:
            if 'maps/t3_rift.bsp' in archive.namelist():
                data = archive.read('maps/t3_rift.bsp')
    if data is None:
        raise RuntimeError('Rift BSP not found')

    def lump(i):
        offset, size = struct.unpack_from('<ii', data, 8 + 8*i)
        return data[offset:offset+size]

    shaders, vertices, indices, surfaces = (lump(i) for i in (1,10,11,13))
    records = []
    for face in range(len(surfaces)//148):
        shader, _, kind, first, count, fi, ni = struct.unpack_from('<7i', surfaces, face*148)
        name = shaders[shader*72:shader*72+64].split(b'\0')[0]
        if kind != 1 or name != b'textures/rift/rock3_phong':
            continue
        normal = struct.unpack_from('<3f', surfaces, face*148+128)
        records.append(' '.join(map(str,(face,count,ni,*normal))))
        for i in range(count):
            records.append(' '.join(map(str,struct.unpack_from('<3f',vertices,(first+i)*80))))
        records.append(' '.join(map(str,struct.unpack_from(f'<{ni}i',indices,fi*4))))
    source = (ROOT/'OpenJK/code/rd-vulkan/vk_backend.cpp').read_text()
    fixture = r'''
#include "vk_rock_boundary.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iostream>
using byte=unsigned char;
constexpr int MST_PLANAR=1, PRINT_ALL=0;
int LittleLong(int n) { return n; }
float LittleFloat(float n) { return n; }
struct dsurface_t { int surfaceType=1; float lightmapVecs[3][3]{}; };
struct vk_world_vertex_t { float position[3]; float attributes[17]; };
struct Batch {
    bool riftSeamProbe=true;
    unsigned surfaceIndex=0, firstIndex=0, indexCount=0;
    float mins[3]{1e9f,1e9f,1e9f}, maxs[3]{-1e9f,-1e9f,-1e9f};
};
struct vk_world_geometry_t { std::vector<Batch> batches; };
struct Imports {
    template<class... A> void Printf(int, const char* format, A... args) {
        std::printf(format,args...);
    }
} ri;
void run(vk_world_geometry_t& world, const dsurface_t* surfaces,
    std::vector<vk_world_vertex_t>& vertices, const std::vector<uint32_t>& indices) {
''' + body(source,'VK_WorldJoinRiftBoundaries') + r'''
}
int main() {
    vk_world_geometry_t world;
    std::vector<dsurface_t> surfaces(20000);
    std::vector<vk_world_vertex_t> vertices;
    std::vector<uint32_t> indices;
    unsigned face,n,ni;
    while (std::cin>>face>>n>>ni) {
        assert(face<surfaces.size());
        Batch b; b.surfaceIndex=face; b.firstIndex=indices.size(); b.indexCount=ni;
        for (float& v:surfaces[face].lightmapVecs[2]) std::cin>>v;
        unsigned base=vertices.size();
        for (unsigned i=0;i<n;++i) {
            vk_world_vertex_t v{};
            for (float& p:v.position) std::cin>>p;
            for (int k=0;k<17;++k) v.attributes[k]=float(base+i+k);
            vertices.push_back(v);
        }
        for (unsigned i=0;i<ni;++i) {
            unsigned index; std::cin>>index; assert(index<n);
            indices.push_back(base+index);
        }
        world.batches.push_back(b);
    }
    auto original=vertices; const auto originalIndices=indices;
    run(world,surfaces.data(),vertices,indices);
    assert(indices==originalIndices && vertices.size()==original.size());
    unsigned moved=0; float maxMove=0;
    for (size_t i=0;i<vertices.size();++i) {
        const auto& a=original[i]; const auto& b=vertices[i];
        assert(!std::memcmp(a.attributes,b.attributes,sizeof(a.attributes)));
        assert(a.position[2]==b.position[2]);
        float d=std::hypot(a.position[0]-b.position[0],a.position[1]-b.position[1]);
        assert(d<=.25f); moved+=d>0; maxMove=std::max(maxMove,d);
    }
    assert(moved>0 && maxMove==.25f);
    auto joined=vertices;
    run(world,surfaces.data(),vertices,indices);
    assert(!std::memcmp(joined.data(),vertices.data(),vertices.size()*sizeof(vertices[0])));
    vertices=original;
    for (auto& b:world.batches) b.riftSeamProbe=false;
    run(world,surfaces.data(),vertices,indices);
    assert(!std::memcmp(original.data(),vertices.data(),vertices.size()*sizeof(vertices[0])));
    std::printf("verified moved=%u max=%.3f attributes/indices preserved; idempotent; excluded batches unchanged\n",moved,maxMove);
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp)/'audit.cpp', Path(tmp)/'audit'
        src.write_text(fixture)
        subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-g',
                        '-I',str(ROOT/'OpenJK/code/rd-vulkan'),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],input='\n'.join(records)+'\n',text=True,check=True)


if __name__ == '__main__':
    main()
