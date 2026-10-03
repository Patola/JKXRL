#!/usr/bin/env python3
"""Synthetic RBSP checks; no retail assets required or modified."""
from contextlib import redirect_stdout
import io
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from audit_remaining_materials import audit, lump, matches, translated_uv


def vertices():
    data = bytearray(3 * 80)
    for v in range(3):
        struct.pack_into('<2f', data, v*80+20, v*.25, v*.125)
        struct.pack_into('<2f', data, v*80+28, v*.25+.5, v*.125+.25)
        data[v*80+68:v*80+71] = bytes((16, 32, 64))
    return data


def bsp():
    shader = b'test/material'.ljust(72, b'\0')
    fog = bytearray(72)
    struct.pack_into('<i', fog, 64, 0)
    surfaces = bytearray(2 * 148)
    for i in range(2):
        offset = i*148
        struct.pack_into('<5i', surfaces, offset, 0, 0, 1, 0, 3)
        surfaces[offset+28:offset+36] = bytes((0, 1, 254, 254))*2
        struct.pack_into('<4i', surfaces, offset+36,
                         *((-3, -3, -3, -3) if i == 0 else (0, 1, -1, -1)))
    result = bytearray(8+18*8)
    result[:4] = b'RBSP'
    struct.pack_into('<i', result, 4, 1)
    for index, data in ((1, shader), (10, vertices()), (12, fog), (13, surfaces)):
        struct.pack_into('<ii', result, 8+index*8, len(result), len(data))
        result.extend(data)
    return result


class MaterialAuditTests(unittest.TestCase):
    def test_exact_token_matching(self):
        self.assertTrue(matches(['map', 'a', 'tcgen', 'vector'], ('tcgen', 'vector')))
        self.assertFalse(matches(['tcgen', 'environment'], ('tcgen', 'vector')))
        self.assertFalse(matches(['tcgenvector'], ('tcgen', 'vector')))

    def test_translated_and_distorted_uvs(self):
        data = vertices()
        self.assertTrue(translated_uv(data, 0, 3, 1))
        struct.pack_into('<f', data, 80+28, .9)
        self.assertFalse(translated_uv(data, 0, 3, 1))
        struct.pack_into('<f', data, 80+28, float('nan'))
        self.assertFalse(translated_uv(data, 0, 3, 1))

    def test_invalid_vertex_range(self):
        for first, count in ((-1, 1), (0, 0), (2, 2)):
            with self.assertRaises(ValueError):
                translated_uv(vertices(), first, count, 1)

    def test_lump_bounds_and_stride(self):
        data = bsp()
        self.assertEqual(len(lump(data, 10, 80)), 240)
        for offset, size in ((-1, 80), (len(data), 80), (152, 81), (152, -1)):
            struct.pack_into('<ii', data, 8+10*8, offset, size)
            with self.assertRaises(ValueError):
                lump(data, 10, 80)

    def test_archive_scan_and_precedence(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            with zipfile.ZipFile(base/'assets0.pk3', 'w') as archive:
                archive.writestr('shaders/test.shader',
                                 'test/material { { tcGen vector } }')
                archive.writestr('maps/test.bsp', bsp())
                archive.writestr('maps/mp/test.bsp', bsp())
                archive.writestr('maps/ffa_test.bsp', bsp())
            with zipfile.ZipFile(base/'assets1.pk3', 'w') as archive:
                archive.writestr('shaders/test.shader',
                                 'test/material { { tcMod entityTranslate '
                                 'surfaceSprites vertical 1 2 3 4 } }')
            output = io.StringIO()
            with redirect_stdout(output):
                audit(base)
            report = output.getvalue()
            for expected in (
                'maps 1\n',
                'vector_uv definitions= 0 BSP surfaces= 0',
                'entity_uv definitions= 1 BSP surfaces= 2',
                'secondary_vertex_nonzero 1\n',
                'secondary_lightmap_surfaces 1\n',
                'local_fog_sprite_map maps/test.bsp 2',
            ):
                self.assertIn(expected, report)
            self.assertNotIn('nontranslated_lightmap_surfaces', report)


if __name__ == '__main__':
    unittest.main()
