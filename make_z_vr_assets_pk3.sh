#!/bin/sh
# Pack the z_vr_assets_* source directories into .pk3 files (zip archives
# with the asset directories at the archive root) and places them in assets/.
set -e

cd "$(dirname "$0")"

command -v zip >/dev/null 2>&1 || {
	echo "error: 'zip' is required (e.g. pacman -S zip / apt install zip)" >&2
	exit 1
}

mkdir -p assets

for name in z_vr_assets_base z_vr_assets_jka z_vr_assets_jko; do
	rm -f "assets/$name.pk3"
	(CDPATH= cd "./$name" && zip -qr "../assets/$name.pk3" .)
	echo "built assets/$name.pk3"
done
