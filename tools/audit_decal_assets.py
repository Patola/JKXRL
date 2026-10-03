#!/usr/bin/env python3
"""Read-only audit of stock polygon-offset blended BSP materials in both games.

Usage: python3 tools/audit_decal_assets.py /path/to/GameData/base
Lists campaign map usage, including depth-writing decals susceptible to blocking
a receiver's finishing stages. Does not modify or copy proprietary assets.
"""
import argparse
import collections
from pathlib import Path
import re
import struct
import zipfile


def definitions(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    tokens = re.findall(r'"[^"\n]*"|[{}]|[^\s{}]+', text)
    i = 0
    while i + 1 < len(tokens):
        name = tokens[i].strip('"').lower()
        if tokens[i+1] != '{':
            i += 1
            continue
        i += 2
        depth, outer, stages = 1, [], []
        while i < len(tokens) and depth:
            token = tokens[i].strip('"').lower()
            i += 1
            if token == '{':
                depth += 1
                if depth == 2:
                    stages.append([])
            elif token == '}':
                depth -= 1
            elif depth == 1:
                outer.append(token)
            elif depth == 2:
                stages[-1].append(token)
        yield name, outer, stages


def audit(base):
    shaders, maps = {}, {}
    for path in sorted(base.glob('assets*.pk3')):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if name.endswith('.shader'):
                    for key, outer, stages in definitions(archive.read(name).decode('latin1')):
                        shaders[key] = (outer, stages)
                elif (name.startswith('maps/') and name.endswith('.bsp') and '/mp/' not in name
                      and not Path(name).stem.startswith(('ctf_', 'ffa_', 'duel_', 'siege_'))):
                    maps[name] = path
    decals = {}
    for name, (outer, stages) in shaders.items():
        if 'polygonoffset' not in outer or not stages:
            continue
        # No opaque stage: these are layered paints/signs, not solid cutout walls.
        if any('blendfunc' not in s or
               s[s.index('blendfunc')+1:s.index('blendfunc')+3] == ['gl_one', 'gl_zero']
               for s in stages):
            continue
        decals[name] = any('depthwrite' in s for s in stages)
    used = collections.defaultdict(collections.Counter)
    for name, path in sorted(maps.items()):
        with zipfile.ZipFile(path) as archive:
            data = archive.read(name)
        if data[:4] != b'RBSP':
            continue
        lumps = [struct.unpack_from('<ii', data, 8+i*8) for i in range(18)]
        for off, size in lumps:
            if min(off, size) < 0 or off+size > len(data):
                raise ValueError(f'{name}: invalid BSP lump')
        off, size = lumps[1]
        names = [data[i:i+64].split(b'\0')[0].decode('latin1').lower()
                 for i in range(off, off+size, 72)]
        off, size = lumps[13]
        for i in range(off, off+size, 148):
            shader, _, kind = struct.unpack_from('<3i', data, i)
            if kind not in (1, 2, 3) or not 0 <= shader < len(names):
                continue
            if names[shader] in decals:
                used[names[shader]][name] += 1
    for name, usage in sorted(used.items()):
        print(f'{name}: depthWrite={int(decals[name])} surfaces={sum(usage.values())} '
              + ', '.join(f'{Path(m).stem}({n})' for m, n in sorted(usage.items())))
    print(f'{base}: {len(used)} used blended polygon-offset materials; '
          f'{sum(decals[n] for n in used)} depth-writing; '
          f'{sum(sum(u.values()) for u in used.values())} surfaces')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    audit(parser.parse_args().base)
