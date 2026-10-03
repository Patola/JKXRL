#!/usr/bin/env python3
"""Read-only hanging-vine test candidates with BSP collision/approach checks.

Triangle centers are candidate anchors, not a reconstruction of random plant
placement. Gameplay/headset verification is still required.
"""
import argparse
from collections import Counter
import math
from pathlib import Path
import struct
import zipfile
from audit_remaining_materials import lump


class Map:
    def __init__(self, base, name):
        data = None
        for path in sorted(base.glob('assets*.pk3')):
            with zipfile.ZipFile(path) as archive:
                key = f'maps/{name}.bsp'
                if key in archive.namelist():
                    data = archive.read(key)
        if data is None:
            raise ValueError(f'Missing map {name}')
        self.data = data
        for attr, index, fmt in [('planes',2,'<4f'), ('nodes',3,'<9i'),
                ('leaves',4,'<12i'), ('leaf_brushes',6,'<i'), ('brushes',8,'<3i'),
                ('sides',9,'<3i'), ('shaders',1,'<64s2i'), ('vertices',10,'<16f16B'),
                ('indices',11,'<i'), ('fogs',12,'<64sii')]:
            setattr(self, attr, list(struct.iter_unpack(fmt, lump(data,index,struct.calcsize(fmt)))))
        self.surfaces = lump(data,13,148)

    def contents(self, point):
        node = 0
        while node >= 0:
            n = self.nodes[node]
            p = self.planes[n[0]]
            node = n[1 if sum(point[a]*p[a] for a in range(3)) >= p[3] else 2]
        leaf = self.leaves[-1-node]
        contents = 0
        for i in range(leaf[10],leaf[10]+leaf[11]):
            first,count,shader = self.brushes[self.leaf_brushes[i][0]]
            if all(sum(point[a]*self.planes[self.sides[k][0]][a] for a in range(3)) <=
                   self.planes[self.sides[k][0]][3]+.01 for k in range(first,first+count)):
                contents |= self.shaders[shader][2]
        return contents,leaf[0]

    def clear(self, point):
        contents,cluster = self.contents(point)
        return not contents & 1 and cluster >= 0

    def approach(self, eye, target):
        return all(self.clear([eye[a]+(target[a]-eye[a])*t/100 for a in range(3)])
                   for t in range(95))

    def candidates(self):
        result = []
        for index in range(len(self.surfaces)//148):
            shader,fog,kind,first,count,fi,ni = struct.unpack_from('<7i',self.surfaces,index*148)
            name = self.shaders[shader][0].split(b'\0')[0].decode()
            if 'vines' not in name or fog < 0 or kind == 2:
                continue
            for j in range(fi,fi+ni,3):
                points = [self.vertices[first+self.indices[k][0]] for k in range(j,j+3)]
                normal = struct.unpack_from('<3f',self.surfaces,index*148+128) if kind == 1 else tuple(
                    sum(p[13+a] for p in points)/3 for a in range(3))
                if normal[2] > -.5:
                    continue
                area = abs((points[2][0]-points[0][0])*(points[1][1]-points[0][1]) -
                           (points[2][1]-points[0][1])*(points[1][0]-points[0][0]))
                if area < 500:
                    continue
                center = [sum(p[a] for p in points)/3 for a in range(3)]
                plant = [center[0],center[1],center[2]-20]
                eye = [round(center[a]+normal[a]*100) for a in range(3)]
                if not self.clear(plant) or not self.clear(eye) or not self.approach(eye,plant):
                    continue
                yaw = round(math.degrees(math.atan2(plant[1]-eye[1],plant[0]-eye[0])))
                result.append(dict(area=round(area), surface=index, fog=fog,
                    local=self.fogs[fog][1]>=0, boundary=self.fogs[fog][2],
                    anchor=[round(a,1) for a in center], viewpos=eye+[yaw]))
        return sorted(result,key=lambda r:r['area'],reverse=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base',type=Path)
    parser.add_argument('--map',default='yavin_swamp')
    args = parser.parse_args()
    records = Map(args.base,args.map).candidates()
    print('Candidates by fog:',dict(Counter(r['fog'] for r in records)))
    print('Largest local candidates:')
    for record in [r for r in records if r['local']][:10]: print(record)
    print('Candidates with exposed boundary:')
    for record in [r for r in records if r['local'] and r['boundary']>=0][:12]: print(record)
