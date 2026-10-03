#!/usr/bin/env python3
"""Keep player-facing release identity and stable packaging in sync."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / 'OpenJK/shared/qcommon/jkxrl_version.h').read_text()
version = re.search(r'#define JKXRL_VERSION "([^"]+)"', header)[1]
parts = [int(re.search(rf'#define JKXRL_VERSION_{p} (\d+)', header)[1])
         for p in ('MAJOR', 'MINOR', 'PATCH')]
assert tuple(map(int, version.split('.'))) in (tuple(parts), tuple(parts[:2]))
if len(version.split('.')) == 2:
    assert parts[2] == 0
pkg = (ROOT / 'packaging/arch/PKGBUILD').read_text()
assert re.search(r'^pkgver=(\S+)', pkg, re.M)[1] == version
assert '#tag=v${pkgver}' in pkg
assert 'pkgname=jkxrl\n' in pkg
assert "conflicts=('jkxrl-git')" in pkg
stv = (ROOT / 'OpenJK/code/qcommon/stv_version.h').read_text()
assert 'JKXRL_DISPLAY_VERSION' in stv and 'JKXR_VERSION' not in stv
assert 'JKXRL_DISPLAY_VERSION " CONSOLE"' in (ROOT / 'OpenJK/code/client/cl_console.cpp').read_text()
for game in ('jka', 'jko'):
    credits = (ROOT / f'z_vr_assets_{game}/ui/credits.menu').read_text()
    assert f'"JKXRL {version}"' in credits
    assert 'Based on JKXR by Team Beef' in credits and 'Patola' in credits
for lang in ('english', 'german', 'french', 'spanish'):
    menus = (ROOT / f'z_vr_assets_jka/strings/{lang}/menus_vr.str').read_text()
    assert '"JKXRL Help"' in menus and 'restart JKXR.' not in menus
menus = (ROOT / 'z_vr_assets_jko/strip/menus_vr.sp').read_text()
assert '"JKXRL Help"' in menus and 'restart JKXR.' not in menus
for document in ('README.md', 'RELEASE_NOTES.md', 'packaging/arch/jkxr.install'):
    assert f'JKXRL {version}' in (ROOT / document).read_text()
print(f'PASS: JKXRL {version} identity, both games, upstream credit and stable package')
