#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>
#
# Build the libraries jplacer links on Windows, static, with MinGW-w64 (the Windows SDK's toolchain): zlib,
# libpng, libjpeg-turbo, Expat, OpenSSL and SQLite, the ones Linux has from the system. Once; then
# packaging/build-windows.sh builds jplacer.exe against them.
#
#   packaging/build-windows-deps.sh      -> $HOME/jplacer-deps-win (sources in $HOME/src/windows-deps)
set -euo pipefail

PREFIX="${DEPS_PREFIX:-$HOME/jplacer-deps-win}"
SRC="${DEPS_SRC:-$HOME/src/windows-deps}"
TOOLCHAIN="${JFRAMEWORK_SDK_WIN:-$HOME/jframework-sdk-win}/mingw-w64.cmake"
HOST=x86_64-w64-mingw32
JOBS=$(nproc)

ZLIB=1.3.1
PNG=1.6.43
JPEG=3.0.3
EXPAT=2.6.2
OPENSSL=3.0.15
SQLITE=3460100   # 3.46.1, as sqlite.org names its amalgamation
SQLITE_YEAR=2024

mkdir -p "$SRC" "$PREFIX"
fetch() {   # url, directory it unpacks to
    [ -d "$SRC/$2" ] || curl -sSL "$1" | tar xz -C "$SRC"
}
cmakeBuild() {   # source directory, cmake options...
    local dir="$1"; shift
    cmake -S "$SRC/$dir" -B "$SRC/$dir/build-win" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF "$@" >/dev/null
    cmake --build "$SRC/$dir/build-win" -j "$JOBS" >/dev/null
    cmake --install "$SRC/$dir/build-win" >/dev/null
}

fetch "https://github.com/madler/zlib/releases/download/v$ZLIB/zlib-$ZLIB.tar.gz" "zlib-$ZLIB"
cmakeBuild "zlib-$ZLIB" -DZLIB_BUILD_EXAMPLES=OFF
# zlib's CMake installs its static library as libzlibstatic.a: also as the name the others look for.
cp "$PREFIX/lib/libzlibstatic.a" "$PREFIX/lib/libz.a"
rm -f "$PREFIX/lib/libzlib.dll.a" "$PREFIX/bin/libzlib.dll"
echo "zlib $ZLIB"

fetch "https://download.sourceforge.net/libpng/libpng-$PNG.tar.gz" "libpng-$PNG"
cmakeBuild "libpng-$PNG" -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_EXECUTABLES=OFF \
    -DZLIB_LIBRARY="$PREFIX/lib/libz.a" -DZLIB_INCLUDE_DIR="$PREFIX/include"
echo "libpng $PNG"

fetch "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$JPEG/libjpeg-turbo-$JPEG.tar.gz" "libjpeg-turbo-$JPEG"
cmakeBuild "libjpeg-turbo-$JPEG" -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_SIMD=OFF
echo "libjpeg-turbo $JPEG"

fetch "https://github.com/libexpat/libexpat/releases/download/R_${EXPAT//./_}/expat-$EXPAT.tar.gz" "expat-$EXPAT"
cmakeBuild "expat-$EXPAT" -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF \
    -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_DOCS=OFF
echo "expat $EXPAT"

fetch "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL/openssl-$OPENSSL.tar.gz" "openssl-$OPENSSL"
(
    cd "$SRC/openssl-$OPENSSL"
    ./Configure mingw64 --cross-compile-prefix="$HOST-" --prefix="$PREFIX" --libdir=lib no-shared no-tests >/dev/null
    make -j "$JOBS" >/dev/null
    make install_sw >/dev/null
)
echo "openssl $OPENSSL"

# SQLite: its amalgamation, one file, compiled to a static library (as Linux's libsqlite3.a).
if [ ! -d "$SRC/sqlite-amalgamation-$SQLITE" ]; then
    curl -sSL -o "$SRC/sqlite.zip" "https://www.sqlite.org/$SQLITE_YEAR/sqlite-amalgamation-$SQLITE.zip"
    (cd "$SRC" && unzip -q sqlite.zip && rm sqlite.zip)
fi
(
    cd "$SRC/sqlite-amalgamation-$SQLITE"
    "$HOST-gcc" -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -c sqlite3.c -o sqlite3.o
    "$HOST-ar" rcs "$PREFIX/lib/libsqlite3.a" sqlite3.o
    cp sqlite3.h sqlite3ext.h "$PREFIX/include/"
)
echo "sqlite $SQLITE"

echo "Windows libraries installed in $PREFIX"
