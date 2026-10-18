#!/usr/bin/env bash
# Builds everything and creates dist/MHWVroidHunter-<version>.zip
#   scripts/package.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-1.0.0}"
JOBS="$(nproc 2>/dev/null || echo 4)"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$JOBS"
./build/vh_tests

cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake >/dev/null
cmake --build build-win -j"$JOBS" --target MHWVroidHunterSetup VRoidHunterConverter vroidhunter-cli

OUT="dist/MHWVroidHunter-$VERSION"
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/manual-install/nativePC/plugins/MHWVroidHunter" "$OUT/manual-install/VRoidHunter" "$OUT/tools"
x86_64-w64-mingw32-strip -o "$OUT/MHWVroidHunterSetup.exe" build-win/MHWVroidHunterSetup.exe
x86_64-w64-mingw32-strip -o "$OUT/manual-install/dinput8.dll" build-win/dinput8.dll
x86_64-w64-mingw32-strip -o "$OUT/manual-install/nativePC/plugins/MHWVroidHunter.dll" build-win/MHWVroidHunter.dll
x86_64-w64-mingw32-strip -o "$OUT/manual-install/nativePC/plugins/MHWVroidHunter/VRoidHunterConverter.exe" build-win/VRoidHunterConverter.exe
x86_64-w64-mingw32-strip -o "$OUT/tools/vroidhunter-cli.exe" build-win/vroidhunter-cli.exe
printf 'Copy your VRoid model (.vrm) into this folder, then start Monster Hunter: World.\r\nIt is converted automatically when the game starts.\r\n' > "$OUT/manual-install/VRoidHunter/PUT YOUR .VRM FILE HERE.txt"
cp docs/README.txt "$OUT/README.txt"
cp LICENSE "$OUT/LICENSE.txt"
cp THIRD_PARTY_NOTICES.md "$OUT/THIRD_PARTY_NOTICES.md"
(cd dist && zip -qr "MHWVroidHunter-$VERSION.zip" "MHWVroidHunter-$VERSION")
echo "created dist/MHWVroidHunter-$VERSION.zip"
