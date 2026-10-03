#!/usr/bin/env python3
"""Bounded read-only stock BSP/material audit, not a visibility/parity certificate.

Counts actual non-MP BSP references separately from shader definitions. Models,
scripts, loose overrides and dynamically registered materials are not enumerated.
"""
import argparse
from collections import Counter, defaultdict
import math
from pathlib import Path
import struct
import zipfile
from audit_decal_assets import definitions


PATTERNS = {
    'vector_uv': ('tcgen', 'vector'),
    'entity_uv': ('tcmod', 'entitytranslate'),
    'inverse_vertex_rgb': ('rgbgen', 'oneminusvertex'),
    'inverse_entity_rgb': ('rgbgen', 'oneminusentity'),
    'inverse_vertex_alpha': ('alphagen', 'oneminusvertex'),
    'portal_alpha': ('alphagen', 'portal'),
    'alpha_noise': ('alphagen', 'wave', 'noise'),
}


def matches(stage, words):
    return any(tuple(stage[i:i+len(words)]) == words for i in range(len(stage)))


def lump(data, index, stride):
    start, size = struct.unpack_from('<ii', data, 8 + index * 8)
    if start < 0 or size < 0 or start + size > len(data) or size % stride:
        raise ValueError(f'invalid lump {index}')
    return memoryview(data)[start:start+size]


def translated_uv(vertices, first, count, slot):
    if first < 0 or count < 1 or first + count > len(vertices)//80:
        raise ValueError('invalid surface vertex range')
    delta = None
    for v in range(first, first+count):
        base = struct.unpack_from('<2f', vertices, v*80+20)
        extra = struct.unpack_from('<2f', vertices, v*80+20+slot*8)
        d = tuple(extra[a]-base[a] for a in range(2))
        if delta is None:
            delta = d
        if any(not math.isfinite(x) or abs(x-delta[a]) >= .0001 for a, x in enumerate(d)):
            return False
    return True


def audit(base):
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
    candidates = {key: {name for name, stages in shaders.items()
                       if any(matches(stage, pattern) for stage in stages)}
                  for key, pattern in PATTERNS.items()}
    uses = defaultdict(Counter)
    stats = Counter()
    fog_sprite_maps = Counter()
    examples = defaultdict(list)
    for name, path in sorted(maps.items()):
        with zipfile.ZipFile(path) as archive:
            data = archive.read(name)
        if data[:4] != b'RBSP':
            continue
        stats['maps'] += 1
        surfaces, vertices, shader_lump, fogs = (lump(data, i, s) for i, s in
                                               ((13, 148), (10, 80), (1, 72), (12, 72)))
        names = [bytes(shader_lump[i:i+64]).split(b'\0')[0].decode('latin1').lower()
                 for i in range(0, len(shader_lump), 72)]
        local = {i//72 for i in range(0, len(fogs), 72)
                 if struct.unpack_from('<i', fogs, i+64)[0] >= 0}
        for offset in range(0, len(surfaces), 148):
            shader, fog, kind, first, count = struct.unpack_from('<5i', surfaces, offset)
            if kind not in (1, 2, 3) or count <= 0:
                continue
            if not 0 <= shader < len(names) or first < 0 or first+count > len(vertices)//80:
                raise ValueError(f'{name}: invalid surface')
            material = names[shader]
            record = f'{name} surface={offset//148} {material}'
            for key, affected in candidates.items():
                if material in affected:
                    uses[key][(name, material)] += 1
            lmstyles = surfaces[offset+28:offset+32]
            vstyles = surfaces[offset+32:offset+36]
            lmnums = struct.unpack_from('<4i', surfaces, offset+36)
            if lmnums[0] == -3:
                slots = [i for i in range(1, 4) if vstyles[i] < 64]
                if slots:
                    stats['secondary_vertex_surfaces'] += 1
                    colored = any(any(vertices[v*80+64+i*4:v*80+67+i*4])
                                  for v in range(first, first+count) for i in slots)
                    if colored:
                        stats['secondary_vertex_nonzero'] += 1
                        if len(examples['secondary_vertex_nonzero']) < 12:
                            examples['secondary_vertex_nonzero'].append(record)
            else:
                slots = [i for i in range(1, 4) if lmstyles[i] < 64 and lmnums[i] >= 0]
                if slots:
                    stats['secondary_lightmap_surfaces'] += 1
                    if not all(translated_uv(vertices, first, count, i) for i in slots):
                        stats['nontranslated_lightmap_surfaces'] += 1
                        if len(examples['nontranslated_lightmap_surfaces']) < 12:
                            examples['nontranslated_lightmap_surfaces'].append(record)
            if fog in local and any('surfacesprites' in s for s in shaders.get(material, [])):
                stats['local_fog_surface_sprite_parents'] += 1
                fog_sprite_maps[name] += 1
                if len(examples['local_fog_surface_sprite_parents']) < 12:
                    examples['local_fog_surface_sprite_parents'].append(record)
    print(base)
    for key in PATTERNS:
        print(key, 'definitions=', len(candidates[key]), 'BSP surfaces=', sum(uses[key].values()))
        for (name, material), count in sorted(uses[key].items()):
            print(' ', name, material, count)
    for key, count in sorted(stats.items()):
        print(key, count)
        for example in examples[key]:
            print(' ', example)
    for name, count in sorted(fog_sprite_maps.items()):
        print('local_fog_sprite_map', name, count)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    audit(parser.parse_args().base)
