#!/usr/bin/env python3
"""Read retail BSP fog brushes and exercise the production volume builder.

Retail files are read only; generated fixtures stay in a temporary directory.
This does not replace runtime loader, model selection or headset verification.
"""
import argparse
from collections import Counter
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-base', type=Path, required=True)
    parser.add_argument('--point', type=float, nargs=3,
                        help='Report nearby surfaces using a fog material, including unassigned pools')
    parser.add_argument('maps', nargs='+')
    args = parser.parse_args()
    wanted = {'maps/' + name + '.bsp' for name in args.maps}
    maps = {}
    for path in sorted(args.game_base.glob('*.pk3')):
        with zipfile.ZipFile(path) as archive:
            for name in wanted.intersection(archive.namelist()):
                maps[name] = archive.read(name)
    if set(maps) != wanted:
        raise RuntimeError('Missing maps: ' + str(wanted - set(maps)))

    records = []
    for name, data in sorted(maps.items()):
        def lump(index, stride):
            offset, size = struct.unpack_from('<ii', data, 8 + index * 8)
            assert 0 <= offset <= len(data) and 0 <= size <= len(data) - offset
            assert size % stride == 0
            return data[offset:offset + size]
        fogs, brushes, sides, planes, surfaces = (
            lump(i, stride) for i, stride in ((12, 72), (8, 12), (9, 12), (2, 16), (13, 148)))
        counts = Counter(struct.unpack_from('<i', surfaces, i + 4)[0]
                         for i in range(0, len(surfaces), 148))
        local_count = 0
        for index in range(len(fogs) // 72):
            material, brush, visible = struct.unpack_from('<64sii', fogs, index * 72)
            if brush == -1:
                continue
            assert 0 <= brush < len(brushes) // 12
            first, count, _ = struct.unpack_from('<iii', brushes, brush * 12)
            assert first >= 0 and count >= 6 and first + count <= len(sides) // 12
            boundary = []
            for side in range(first, first + count):
                plane = struct.unpack_from('<i', sides, side * 12)[0]
                assert 0 <= plane < len(planes) // 16
                boundary.append(struct.unpack_from('<4f', planes, plane * 16))
            mins = [-boundary[axis * 2][3] for axis in range(3)]
            maxs = [boundary[axis * 2 + 1][3] for axis in range(3)]
            print(name, 'fog', index, material.split(b'\0')[0].decode(),
                  'surfaces', counts[index], 'bounds', mins, maxs, 'visible', visible,
                  flush=True)
            records.append(f'{count} {visible}')
            records.extend(' '.join(map(str, p)) for p in boundary)
            local_count += 1
        print(name, 'local volumes:', local_count, flush=True)
        if args.point:
            vertices, shaders = lump(10, 80), lump(1, 72)
            fog_materials = {fogs[i:i + 64].split(b'\0')[0]
                             for i in range(0, len(fogs), 72)}
            nearby = []
            for face in range(len(surfaces) // 148):
                shader, fog, kind, first, count = struct.unpack_from('<5i', surfaces, face * 148)
                material = shaders[shader * 72:shader * 72 + 64].split(b'\0')[0]
                if material not in fog_materials or count <= 0:
                    continue
                points = [struct.unpack_from('<3f', vertices, (first + j) * 80)
                          for j in range(count)]
                lo = [min(p[a] for p in points) for a in range(3)]
                hi = [max(p[a] for p in points) for a in range(3)]
                distance = sum(max(lo[a] - args.point[a], 0, args.point[a] - hi[a]) ** 2
                               for a in range(3)) ** .5
                nearby.append((distance, face, material.decode(), fog, kind, lo, hi))
            for record in sorted(nearby)[:6]:
                print('nearby (distance, surface, material, fog, type, bounds):', record, flush=True)

    fixture = r'''
#include "vk_local_fog.h"
#include <cassert>
#include <iostream>
int main() {
    int count, side, tested=0;
    while(std::cin >> count >> side) {
        std::vector<vk_local_fog::Plane> planes(count);
        for(auto& p:planes) for(float& v:p) std::cin>>v;
        const auto volume=vk_local_fog::Build(planes,side,{.3f,.6f,.01f},256);
        assert(volume.valid);
        float middle[3];
        for(int i=0;i<3;++i) middle[i]=(volume.mins[i]+volume.maxs[i])*.5f;
        assert(vk_local_fog::Contains(volume,middle));
        if(side>=0) {
            const auto& plane=planes[side];
            const float length2=plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2];
            const float offset=(plane[3]-plane[0]*middle[0]-plane[1]*middle[1]-plane[2]*middle[2])/length2;
            float boundary[3], inside[3], outside[3];
            for(int i=0;i<3;++i) {
                boundary[i]=middle[i]+offset*plane[i];
                inside[i]=boundary[i]-64*plane[i];
                outside[i]=boundary[i]+64*plane[i];
            }
            assert(std::fabs(vk_local_fog::Evaluate(volume.plane,boundary))<.05f);
            auto draw=vk_local_fog::Parameters(volume,outside);
            assert(draw.eye<0);
            assert(vk_local_fog::Amount(128,vk_local_fog::Evaluate(draw.plane,outside),draw.eye,256)==0);
            assert(vk_local_fog::Amount(128,vk_local_fog::Evaluate(draw.plane,inside),draw.eye,256)>0);
        }
        ++tested;
    }
    std::cout << "Production volume builder passed: " << tested << " retail brushes\n";
}
'''
    with tempfile.TemporaryDirectory(prefix='jkxr-local-fog-') as temp:
        directory = Path(temp)
        source, binary = directory / 'audit.cpp', directory / 'audit'
        source.write_text(fixture)
        subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-g', '-I',
                        str(ROOT / 'OpenJK/code/rd-vulkan'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], input='\n'.join(records) + '\n', text=True, check=True)


if __name__ == '__main__':
    main()
