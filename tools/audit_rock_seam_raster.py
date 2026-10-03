#!/usr/bin/env python3
"""Rasterize BOTH Rift strip boundaries from the user's two viewpoints."""
import argparse
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-base', type=Path)
    p.add_argument('--build', type=Path, required=True)
    args = p.parse_args()
    data = None
    for path in sorted(args.game_base.glob('*.pk3')) if args.game_base else []:
        with zipfile.ZipFile(path) as archive:
            if 'maps/t3_rift.bsp' in archive.namelist():
                data = archive.read('maps/t3_rift.bsp')
    if args.game_base and data is None:
        raise RuntimeError('Rift map missing')
    def lump(i):
        offset, length = struct.unpack_from('<ii', data, 8+i*8)
        return data[offset:offset+length]
    triangles = []
    if data:
        surfaces, vertices, indices = (lump(i) for i in (13,10,11))
        for face in (9135,9138,9139):
            _, _, _, first, count, first_index, count_indices = struct.unpack_from('<7i', surfaces, face*148)
            points = [struct.unpack_from('<3f', vertices, (first+i)*80) for i in range(count)]
            for t in range(0,count_indices,3):
                ids = struct.unpack_from('<3i', indices, (first_index+t)*4)
                triangles.append([points[i] for i in ids])
    a = (-1770,1878,-5056)
    b = (-1770,1878,4928)
    c = (-1770.125,1878,-5056)
    d = (-1770.125,1878,4928)
    if not data:
        triangles = [[(-1718.6666,2097.3333,430.6667),a,b],
                     [d,c,(-1740,1544,4928)],
                     [(-1740,1544,4928),c,(-1740,1544,-5056)],
                     [(-1740.5,1544,4928),(-1688,1376,-5056),(-1688,1376,-368)],
                     [(-1740.5,1544,-5056),(-1688,1376,-5056),(-1740.5,1544,4928)]]
    old_join = [[(-1770.0625,y,z) if x in (-1770,-1770.125) and y==1878 else (x,y,z)
                for x,y,z in tri] for tri in triangles]
    fixtures = {
        'original': triangles,
        'old-eighth-only': old_join,
        'both-joined': [[(-1740.25,y,z) if x in (-1740,-1740.5) and y==1544 else (x,y,z)
                        for x,y,z in tri] for tri in old_join],
    }
    for edge, target in [('eighth',(-1770.0625,1878)),('half',(-1740.25,1544))]:
        for label, geometry in fixtures.items():
            records = []
            for base in ((692,1341,630),(129,-1468,-1162)):
                for eye_offset in (-1.25,0,1.25):
                    origin = (base[0]+eye_offset,base[1],base[2])
                    fx,fy = target[0]-origin[0],target[1]-origin[1]
                    length=math.hypot(fx,fy)
                    fx,fy=fx/length,fy/length
                    for shift in range(-32,33):
                        records.append(str(len(geometry)))
                        for tri in geometry:
                            clip=[]
                            for x,y,z in tri:
                                dx,dy,dz=x-origin[0],y-origin[1],z-origin[2]
                                w=fx*dx+fy*dy
                                clip.extend(((fy*dx-fx*dy)*12.5+w*shift/(64*128), -dz*12.5, w-1, w))
                            records.append(' '.join(map(str,clip)))
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'triangles.txt'
                path.write_text('\n'.join(records)+'\n')
                result=subprocess.run([str(args.build/'LightmapGpuTests'),
                    str(args.build/'lightmap-test.vert.spv'),str(args.build/'lightmap-test.frag.spv'),str(path)],
                    capture_output=True,text=True)
                if result.returncode == 77:
                    raise SystemExit(77)
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
            counts=[int(line.split('=')[1]) for line in result.stdout.splitlines() if line.startswith('uncovered=')]
            assert len(counts) == 390, 'Incomplete coverage sweep'
            print(f'{edge}/{label}: frames={len(counts)} frames-with-gaps={sum(n>0 for n in counts)} uncovered-pixels={sum(counts)} worst-frame={max(counts)}')
            if label == 'original' or (edge == 'half' and label == 'old-eighth-only'):
                assert sum(counts) > 0, 'Negative control must reproduce the gap'
            if label == 'both-joined':
                assert sum(counts) == 0, 'Joined edges must remain watertight across the sweep'


if __name__ == '__main__':
    main()
