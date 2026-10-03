#!/usr/bin/env python3
"""Read-only stock BSP vertex-style bounds and lighting contribution audit."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import zipfile
from audit_remaining_materials import lump


def inspect(base, map_name):
    data = None
    for path in sorted(base.glob('assets*.pk3')):
        with zipfile.ZipFile(path) as archive:
            name = f'maps/{map_name}.bsp'
            if name in archive.namelist():
                data = archive.read(name)
    if data is None:
        raise ValueError(f'missing map {map_name}')
    surfaces, vertices, shaders = (lump(data, i, s) for i, s in ((13, 148), (10, 80), (1, 72)))
    records = []
    for offset in range(0, len(surfaces), 148):
        shader, fog, kind, first, count = struct.unpack_from('<5i', surfaces, offset)
        styles = list(surfaces[offset+32:offset+36])
        if struct.unpack_from('<i', surfaces, offset+36)[0] != -3 or styles[1] >= 64:
            continue
        active = next((i for i, s in enumerate(styles) if s >= 64), 4)
        if count <= 0 or first < 0 or first+count > len(vertices)//80:
            raise ValueError('invalid vertices')
        points = [struct.unpack_from('<3f', vertices, v*80) for v in range(first, first+count)]
        rgb = [[sum(vertices[v*80+64+slot*4+c] for v in range(first, first+count))/count
                for c in range(3)] for slot in range(active)]
        records.append(dict(surface=offset//148, kind=kind, count=count, styles=styles[:active],
            shader=bytes(shaders[shader*72:shader*72+64]).split(b'\0')[0].decode(),
            mins=[min(p[a] for p in points) for a in range(3)],
            maxs=[max(p[a] for p in points) for a in range(3)], rgb=rgb))
    return records


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('map')
    args = parser.parse_args()
    records = inspect(args.base, args.map)
    print(json.dumps({'map': args.map, 'kinds': dict(Counter(r['kind'] for r in records)),
                      'surfaces': records}, indent=2))
