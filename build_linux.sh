#!/bin/sh
# Build the native Linux PCVR binaries for both Jedi Academy and Jedi Outcast,
# and pack the VR asset .pk3 files.
#
# Output:
#   Projects/Android/jni/OpenJK/build-linux/openjk_sp.x86_64              (JKA engine)
#   Projects/Android/jni/OpenJK/build-linux/openjo_sp.x86_64              (JKO engine)
#   Projects/Android/jni/OpenJK/build-linux/code/rd-vanilla/rdsp-vanilla_*.so
#   Projects/Android/jni/OpenJK/build-linux/code/rd-vanilla/rdjosp-vanilla_*.so
#   Projects/Android/jni/OpenJK/build-linux/code/game/jagame*.so
#   Projects/Android/jni/OpenJK/build-linux/codeJK2/game/jospgame*.so
#   assets/z_vr_assets_{base,jka,jko}.pk3
#
# Use install_linux.sh afterwards to copy everything into a game directory.
set -e

cd "$(dirname "$0")"

SRC=Projects/Android/jni/OpenJK
BUILD=${BUILD_DIR:-$SRC/build-linux}
JOBS=$(nproc 2>/dev/null || echo 4)

cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
	-DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF \
	-DBuildMPGame=OFF -DBuildMPCGame=OFF -DBuildMPUI=OFF \
	-DBuildSPEngine=ON -DBuildSPGame=ON -DBuildSPRdVanilla=ON \
	-DBuildJK2SPEngine=ON -DBuildJK2SPGame=ON -DBuildJK2SPRdVanilla=ON \
	-DBuildTests=OFF "$@"

cmake --build "$BUILD" -j "$JOBS"

./make_z_vr_assets_pk3.sh

echo
echo "Build complete. Install with:"
echo "  ./install_linux.sh jka \"/path/to/Jedi Academy/GameData\""
echo "  ./install_linux.sh jko \"/path/to/Jedi Outcast/GameData\""
