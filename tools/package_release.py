#!/usr/bin/env python3
"""Make binary archives and checksums from a verified Arch release package."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    version = re.search(r'#define JKXRL_VERSION "([^"]+)"',
                       (root / 'OpenJK/shared/qcommon/jkxrl_version.h').read_text())[1]
    package = args.package.resolve()
    if not re.fullmatch(rf'jkxrl-{re.escape(version)}-\d+-x86_64\.pkg\.tar\.zst', package.name):
        raise ValueError('Not a matching JKXRL x86-64 release package')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    copied = output / package.name
    if package != copied:
        shutil.copy2(package, copied)
    archives = [copied]
    with tempfile.TemporaryDirectory(prefix='jkxrl-release-') as tmp:
        stage = Path(tmp)
        subprocess.run(['bsdtar', '-xf', str(package), '-C', str(stage), 'usr'], check=True)
        expected = ['lib/jkxr/openjk_sp.x86_64', 'lib/jkxr/openjo_sp.x86_64',
                    'lib/jkxr/rdsp-vulkan_x86_64.so', 'lib/jkxr/rdjosp-vulkan_x86_64.so',
                    'lib/jkxr/base/jagamex86_64.so', 'lib/jkxr/base/jospgamex86_64.so',
                    'bin/jkxr-jka', 'bin/jkxr-jko']
        for file in expected:
            if not (stage / 'usr' / file).is_file():
                raise ValueError(f'Missing runtime component: {file}')
        for game in ('jka', 'jko'):
            packs = stage / 'usr/share/jkxr' / game
            names = {'z_vr_assets_base.pk3', f'z_vr_assets_{game}.pk3',
                     f'z_vr_weapons_{game}_Crusty_and_Elin.pk3'}
            if {p.name for p in packs.glob('*.pk3')} != names:
                raise ValueError(f'Unexpected/incomplete VR packs for {game}')
            with zipfile.ZipFile(packs / f'z_vr_assets_{game}.pk3') as asset:
                if f'"JKXRL {version}"'.encode() not in asset.read('ui/credits.menu'):
                    raise ValueError(f'Stale credits in {game} asset pack')
        shutil.copy2(root / 'packaging/binary/INSTALL.txt', stage / 'INSTALL.txt')
        archive_name = f'jkxrl-{version}-linux-x86_64'
        gz = output / f'{archive_name}.tar.gz'
        with tarfile.open(gz, 'w:gz') as archive:
            archive.add(stage / 'usr', arcname='usr')
            archive.add(stage / 'INSTALL.txt', arcname='INSTALL.txt')
        zipped = output / f'{archive_name}.zip'
        with zipfile.ZipFile(zipped, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(stage.rglob('*')):
                archive.write(path, path.relative_to(stage))
        archives += [gz, zipped]
        # Check archive integrity and ensure both formats include all runtime files.
        with tarfile.open(gz) as archive:
            assert all(f'usr/{p}' in archive.getnames() for p in expected)
        with zipfile.ZipFile(zipped) as archive:
            assert archive.testzip() is None
            assert all(f'usr/{p}' in archive.namelist() for p in expected)
    lines = []
    for path in archives:
        with path.open('rb') as file:
            digest = hashlib.file_digest(file, 'sha256').hexdigest()
        lines.append(f'{digest}  {path.name}\n')
        print(path)
    (output / 'SHA256SUMS').write_text(''.join(lines))
    print(output / 'SHA256SUMS')


if __name__ == '__main__':
    main()
