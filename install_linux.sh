#!/bin/sh
# Install the Linux JKXR binaries and VR asset pk3s into a game directory.
#
# Usage:
#   ./install_linux.sh jka "/path/to/Jedi Academy/GameData"
#   ./install_linux.sh jko "/path/to/Jedi Outcast/GameData"
#
# The GameData directory is wherever your (Steam/GOG) copy of the game keeps
# its 'base' folder with assets0.pk3 etc. Typical locations:
#   ~/.local/share/Steam/steamapps/common/Jedi Academy/GameData
#   ~/.local/share/Steam/steamapps/common/Jedi Outcast/GameData
# or under any custom Steam library, e.g.:
#   /games/SteamLibrary/steamapps/common/Jedi Academy/GameData
#
# Layout installed:
#   GameData/<engine binary + renderer .so + gamecode .so>
#   GameData/base/<z_vr_assets_*.pk3, z_vr_weapons_*.pk3>
set -e

cd "$(dirname "$0")"

GAME=$1
DEST=$2
BUILD=${BUILD_DIR:-OpenJK/build-linux}

usage() {
	echo "usage: $0 jka|jko /path/to/GameData" >&2
	exit 1
}

[ -n "$GAME" ] && [ -n "$DEST" ] || usage

case "$GAME" in
	jka)
		ENGINE=openjk_sp.x86_64
		RENDERER=code/rd-vanilla/rdsp-vanilla_x86_64.so
		GAMECODE=code/game/jagamex86_64.so
		GAME_PK3=assets/z_vr_assets_jka.pk3
		WEAPONS_PK3=JKXR-PCVR-Installer/JKA/base/z_vr_weapons_jka_Crusty_and_Elin.pk3
		HOMENAME=openjk
		;;
	jko)
		ENGINE=openjo_sp.x86_64
		RENDERER=code/rd-vanilla/rdjosp-vanilla_x86_64.so
		GAMECODE=codeJK2/game/jospgamex86_64.so
		GAME_PK3=assets/z_vr_assets_jko.pk3
		WEAPONS_PK3=JKXR-PCVR-Installer/JKO/base/z_vr_weapons_jko_Crusty_and_Elin.pk3
		HOMENAME=openjo
		;;
	*)
		usage
		;;
esac

[ -x "$BUILD/$ENGINE" ] || {
	echo "error: $BUILD/$ENGINE not found - run ./build_linux.sh first" >&2
	exit 1
}
[ -f assets/z_vr_assets_base.pk3 ] && [ -f "$GAME_PK3" ] || ./make_z_vr_assets_pk3.sh

[ -d "$DEST" ] || { echo "error: '$DEST' is not a directory" >&2; exit 1; }
[ -f "$DEST/base/assets0.pk3" ] || \
	echo "warning: '$DEST/base/assets0.pk3' not found - is this really the game's GameData directory?" >&2

mkdir -p "$DEST/base"
install -m755 "$BUILD/$ENGINE" "$BUILD/$RENDERER" "$BUILD/$GAMECODE" "$DEST/"
install -m644 assets/z_vr_assets_base.pk3 "$GAME_PK3" "$WEAPONS_PK3" "$DEST/base/"

# OpenJK/OpenJO load pk3s from the home path with HIGHER priority than the game
# directory. If a stale copy of our assets lives there it shadows the freshly
# installed ones, so refresh the home-path copies too whenever that base exists.
HOMEBASE="${XDG_DATA_HOME:-$HOME/.local/share}/$HOMENAME/base"
if [ -d "$HOMEBASE" ]; then
	install -m644 assets/z_vr_assets_base.pk3 "$GAME_PK3" "$WEAPONS_PK3" "$HOMEBASE/"
	echo "Also refreshed VR asset pk3s in home path '$HOMEBASE'."
fi

echo "Installed JKXR ($GAME) into '$DEST'."
echo "Run it with an OpenXR runtime active (WiVRn/Monado/SteamVR), under X11 or XWayland:"
echo "  cd \"$DEST\" && SDL_VIDEODRIVER=x11 ./$ENGINE"
