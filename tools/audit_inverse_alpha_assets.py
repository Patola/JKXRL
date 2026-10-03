#!/usr/bin/env python3
"""Read-only inverse-alpha shader/BSP usage audit; no proprietary asset copying."""
import argparse
from pathlib import Path
import struct
import zipfile
from audit_decal_assets import definitions


def audit(base, specular=False):
    shaders, maps = {}, {}
    for path in sorted(base.glob('assets*.pk3')):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if name.endswith('.shader'):
                    for key, _, stages in definitions(archive.read(name).decode('latin1')):
                        shaders[key] = stages
                elif (name.startswith('maps/') and name.endswith('.bsp') and '/mp/' not in name
                      and not Path(name).stem.startswith(('ctf_', 'ffa_', 'duel_', 'siege_'))):
                    maps[name] = path
    affected = {}
    for name, stages in shaders.items():
        modes = []
        for i, stage in enumerate(stages):
            if specular:
                if 'alphagen' in stage and stage[stage.index('alphagen')+1] == 'lightingspecular':
                    j = stage.index('blendfunc') if 'blendfunc' in stage else -1
                    modes.append((i, stage[j+1:j+3] if j >= 0 else 'opaque'))
                continue
            if 'blendfunc' not in stage:
                continue
            j = stage.index('blendfunc')
            pair = stage[j+1:j+3]
            if (len(pair) == 2 and pair[0] == 'gl_one_minus_src_alpha' and
                    pair[1] in ('gl_src_alpha', 'gl_one_minus_src_alpha')):
                modes.append((i, pair[1]))
        if modes:
            affected[name] = modes
            print(f'definition {name}: {modes}')
    count = 0
    for name, path in sorted(maps.items()):
        with zipfile.ZipFile(path) as archive:
            data = archive.read(name)
        if data[:4] != b'RBSP':
            continue
        lumps = [struct.unpack_from('<ii', data, 8+i*8) for i in range(18)]
        for offset, size in lumps:
            if min(offset, size) < 0 or offset+size > len(data):
                raise ValueError(f'{name}: invalid lump')
        offset, size = lumps[1]
        names = [data[i:i+64].split(b'\0')[0].decode('latin1').lower()
                 for i in range(offset, offset+size, 72)]
        offset, size = lumps[13]
        for i in range(offset, offset+size, 148):
            shader, _, kind, first, number = struct.unpack_from('<5i', data, i)
            if not 0 <= shader < len(names) or names[shader] not in affected or kind not in (1, 2, 3):
                continue
            if first < 0 or number < 1 or first+number > lumps[10][1]//80:
                raise ValueError(f'{name}: invalid vertices')
            points = [struct.unpack_from('<3f', data, lumps[10][0]+v*80)
                      for v in range(first, first+number)]
            mins = tuple(min(p[a] for p in points) for a in range(3))
            maxs = tuple(max(p[a] for p in points) for a in range(3))
            print(f'{name} surface={(i-offset)//148} {names[shader]} bounds={mins}..{maxs}')
            count += 1
    print(f'{base}: definitions={len(affected)} campaign BSP surfaces={count}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('--specular', action='store_true', help='audit lightingSpecular instead')
    args = parser.parse_args()
    audit(args.base, args.specular)
