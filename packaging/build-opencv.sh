#!/usr/bin/env bash
# Build the OpenCV jplacer links: static, only the modules OpenPnP's vision
# pipelines use (core, imgproc, calib3d, features2d, flann, objdetect), with no
# outside libraries (its own zlib), so the AppImage needs nothing more.
#
#   packaging/build-opencv.sh            -> $HOME/opencv-jplacer
#   packaging/build-opencv.sh --windows  -> $HOME/opencv-jplacer-win (MinGW-w64, the Windows SDK's toolchain)
#
# Then configure jplacer with -DCMAKE_PREFIX_PATH="$HOME/jframework-sdk;$HOME/opencv-jplacer"
# (for Windows, see packaging/build-windows.sh).

set -euo pipefail

VERSION=4.10.0
SRC="${OPENCV_SRC:-$HOME/src/opencv-$VERSION}"
PREFIX="${OPENCV_PREFIX:-$HOME/opencv-jplacer}"
BUILD="$SRC/build-jplacer"
CROSS=()
if [ "${1:-}" = "--windows" ]; then
    PREFIX="${OPENCV_PREFIX:-$HOME/opencv-jplacer-win}"
    BUILD="$SRC/build-jplacer-win"
    CROSS=(-DCMAKE_TOOLCHAIN_FILE="${JFRAMEWORK_SDK_WIN:-$HOME/jframework-sdk-win}/mingw-w64.cmake" -DWITH_MSMF=OFF -DWITH_DSHOW=OFF -DWITH_WIN32UI=OFF -DWITH_DIRECTX=OFF)
fi

if [ ! -d "$SRC" ]; then
    mkdir -p "$(dirname "$SRC")"
    curl -sSL "https://github.com/opencv/opencv/archive/refs/tags/$VERSION.tar.gz" | tar xz -C "$(dirname "$SRC")"
fi

cmake -S "$SRC" -B "$BUILD" -G Ninja "${CROSS[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_LIST=core,imgproc,calib3d,features2d,flann,objdetect \
    -DBUILD_ZLIB=ON -DWITH_IPP=OFF -DWITH_TBB=OFF -DWITH_OPENMP=OFF -DWITH_LAPACK=OFF -DWITH_EIGEN=OFF \
    -DWITH_OPENCL=OFF -DWITH_VA=OFF -DWITH_VA_INTEL=OFF -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_FFMPEG=OFF \
    -DWITH_GSTREAMER=OFF -DWITH_V4L=OFF -DWITH_PROTOBUF=OFF -DWITH_QUIRC=ON -DWITH_ITT=OFF -DWITH_ADE=OFF -DWITH_OPENEXR=OFF \
    -DWITH_JPEG=OFF -DWITH_PNG=OFF -DWITH_TIFF=OFF -DWITH_WEBP=OFF -DWITH_OPENJPEG=OFF -DWITH_JASPER=OFF \
    -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF -DBUILD_opencv_apps=OFF \
    -DBUILD_JAVA=OFF -DBUILD_opencv_python3=OFF -DBUILD_opencv_python2=OFF -DOPENCV_GENERATE_PKGCONFIG=OFF
cmake --build "$BUILD"
cmake --install "$BUILD"
echo "OpenCV $VERSION installed in $PREFIX"
