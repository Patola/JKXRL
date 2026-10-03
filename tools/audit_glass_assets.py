#!/usr/bin/env python3
"""Read-only stock BSP glass coverage audit. No proprietary data is copied.

Build the production-helper probe:
  c++ -std=c++17 -O2 tools/check_glass_geometry.cpp -o /tmp/check-glass
Run with a game's GameData/base directory:
  python3 tools/audit_glass_assets.py /tmp/check-glass /path/to/GameData/base
"""
import argparse
import collections
import pathlib
import re
import struct
import subprocess
import zipfile


def audit(probe, base):
    counts = collections.Counter()
    # Ascending archives, last definition wins, matching stock asset precedence.
    maps = {}
    for path in sorted(base.glob('assets*.pk3')):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if (name.startswith('maps/') and name.endswith('.bsp') and
                        '/mp/' not in name and not name.startswith('maps/ffa_')):
                    maps[name] = path
    for name, path in sorted(maps.items()):
        with zipfile.ZipFile(path) as archive:
            data = archive.read(name)
        if data[:4] != b'RBSP':
            raise ValueError(f'{name}: not a Raven BSP')
        lumps = [struct.unpack_from('<ii', data, 8+i*8) for i in range(18)]
        for offset, size in lumps:
            if offset < 0 or size < 0 or offset+size > len(data):
                raise ValueError(f'{name}: invalid lump')
        offset, size = lumps[0]
        entities = data[offset:offset+size].decode('latin1')
        source = []
        for block in re.findall(r'\{([^{}]*)\}', entities):
            entity = dict(re.findall(r'"([^"]*)"\s*"([^"]*)"', block))
            if entity.get('classname') != 'func_glass' or not entity.get('model', '').startswith('*'):
                continue
            model = int(entity['model'][1:])
            if not 0 < model < lumps[7][1] // 40:
                raise ValueError(f'{name}: invalid glass model {model}')
            first, count = struct.unpack_from('<ii', data, lumps[7][0]+model*40+24)
            if first < 0 or count < 0 or first+count > lumps[13][1] // 148:
                raise ValueError(f'{name}: invalid surface range')
            faces = []
            for surface in range(first, first+count):
                offset = lumps[13][0]+surface*148
                kind, first_vertex, vertex_count = struct.unpack_from('<iii', data, offset+8)
                if kind != 1 or vertex_count < 3:
                    continue
                if first_vertex < 0 or first_vertex+vertex_count > lumps[10][1] // 80:
                    raise ValueError(f'{name}: invalid vertex range')
                normal = struct.unpack_from('<3f', data, offset+128)
                points = [struct.unpack_from('<3f', data, lumps[10][0]+v*80)
                          for v in range(first_vertex, first_vertex+vertex_count)]
                faces.append(' '.join(map(str, (vertex_count, *normal, *(x for p in points for x in p)))))
            source.append(f'{model} {len(faces)}\n'+'\n'.join(faces))
        if not source:
            continue
        result = subprocess.run([str(probe)], input='\n'.join(source), text=True,
                                capture_output=True)
        if result.returncode:
            raise ValueError(f'{name}: probe failed ({result.returncode}): {result.stderr}')
        results = result.stdout.splitlines()
        if len(results) != len(source):
            raise ValueError(f'{name}: incomplete probe output')
        for line in results:
            model, valid0, valid1, area0, area1, n0, shards0, n1, shards1 = line.split()
            valid = valid0 == '1' and (valid1 == '1' or float(area1) == 0)
            polygon = int(shards0) > 0 and (int(shards1) > 0 or float(area1) == 0)
            counts['panes'] += 1
            counts['quad' if valid else 'polygon' if polygon else 'unsupported'] += 1
            if not valid:
                bounds = struct.unpack_from('<6f', data, lumps[7][0]+int(model)*40)
                print(f'{name}: *{model}: boundary={n0}/{n1} shards={shards0}/{shards1} bounds={bounds}')
    print(f'{base}: {dict(counts)}')
    if counts['unsupported']:
        raise ValueError('Uncovered glass panes remain')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=pathlib.Path)
    parser.add_argument('base', type=pathlib.Path)
    args = parser.parse_args()
    audit(args.probe.resolve(), args.base)
