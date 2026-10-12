#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>
#
# Build jplacer for Windows (cross-compiled, MinGW-w64) and gather it as a folder and a zip: jplacer.exe, the Vulkan
# loader beside it, and what the AppImage carries beside its executable (the manual, firmware profiles,
# translations, OpenPnP's defaults, icons, illustrations, the BlindsFeeder models, the licences).
#
#   packaging/build-windows.sh [version]   -> dist/jplacer-<version>-setup.exe (the installer, what the updater
#                                             installs) and dist/jplacer-<version>-windows-x64.zip (the same, to
#                                             unzip and run), gathered in dist/windows-<version>/jplacer/
#
# Needs, once: the Windows JFramework SDK ($HOME/jframework-sdk-win), packaging/build-windows-deps.sh and
# packaging/build-opencv.sh --windows; Inno Setup under Wine (its ISCC.exe, $ISCC); and the manual built
# (manual/tools/build.sh). The version is the one given, else CMakeLists.txt's.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fail() { echo "build-windows: $*" >&2; exit 1; }
cd "$ROOT"
source packaging/version.sh
VERSION="${1:-$PLAIN_VERSION}"
DEPS="${DEPS_PREFIX:-$HOME/jplacer-deps-win}"
ISCC="${ISCC:-$HOME/.wine/drive_c/InnoSetup/ISCC.exe}"

[ -f "$DEPS/bin/vulkan-1.dll" ] || fail "no Windows libraries -- run packaging/build-windows-deps.sh first"
[ -f "$ROOT/manual/site/index.html" ] || fail "no built manual -- run manual/tools/build.sh first"
[ -f "$ISCC" ] || fail "no Inno Setup (ISCC.exe) under Wine at $ISCC"

cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/windows.cmake -DCMAKE_BUILD_TYPE=Release \
    -DJPLACER_TESTS=OFF -DJPLACER_VERSION_OVERRIDE="$VERSION" >/dev/null
cmake --build build-win -j "$(nproc)" --target jplacer >/dev/null || fail "the build failed"
# Put back to the plain version, so the next build is as CMakeLists.txt says.
cmake -S . -B build-win -DJPLACER_VERSION_OVERRIDE= >/dev/null

OUT="$ROOT/dist/windows-$VERSION"
DIR="$OUT/jplacer"
rm -rf "$OUT"
mkdir -p "$DIR" "$ROOT/dist/symbols"
cp build-win/jplacer.exe "$ROOT/dist/symbols/jplacer-$VERSION.exe"   # its symbols kept, for a crash's addresses
x86_64-w64-mingw32-strip -o "$DIR/jplacer.exe" build-win/jplacer.exe
cp "$DEPS/bin/vulkan-1.dll" "$DIR/"
cp -r manual/site "$DIR/manual"
cp -r profiles translations icons illustrations openscad "$DIR/"
cp -r openpnp-defaults "$DIR/openpnp-defaults"
find "$DIR/openpnp-defaults" \( -name '*.db' -o -name '*.db-wal' -o -name '*.db-shm' \) -delete
cp LICENSE NOTICE.md "$DIR/"
cp -r licenses "$DIR/licenses"

ZIP="$ROOT/dist/jplacer-$VERSION-windows-x64.zip"
rm -f "$ZIP"
(cd "$OUT" && zip -qr "$ZIP" jplacer)
echo "built: $ZIP"

# The installer: Inno Setup, run under Wine, given Windows' paths to the folder (and the icon beside it).
cp build-win/jplacer.ico "$OUT/jplacer.ico"
SETUP="$ROOT/dist/jplacer-$VERSION-setup.exe"
rm -f "$SETUP"
WINEDEBUG=-all wine "$ISCC" /Q "/DVersion=$VERSION" "/DSource=$(winepath -w "$DIR")" "/DOutput=$(winepath -w "$ROOT/dist")" \
    "$(winepath -w "$ROOT/packaging/windows/jplacer.iss")" >/dev/null 2>&1 || fail "Inno Setup failed"
[ -f "$SETUP" ] || fail "Inno Setup made no installer"
echo "built: $SETUP"
