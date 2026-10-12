# Cross-compiling jplacer for Windows (packaging/build-windows.sh): the Windows JFramework SDK's MinGW-w64
# toolchain, and where the Windows libraries were built (looked in alongside the compiler's own): the SDK, the
# libraries packaging/build-windows-deps.sh builds, and the OpenCV packaging/build-opencv.sh --windows builds.
set(JPLACER_WINDOWS_SDK "$ENV{HOME}/jframework-sdk-win" CACHE PATH "The Windows JFramework SDK")
set(JPLACER_WINDOWS_DEPS "$ENV{HOME}/jplacer-deps-win" CACHE PATH "packaging/build-windows-deps.sh's libraries")
set(JPLACER_WINDOWS_OPENCV "$ENV{HOME}/opencv-jplacer-win" CACHE PATH "packaging/build-opencv.sh --windows's OpenCV")
include("${JPLACER_WINDOWS_SDK}/mingw-w64.cmake")
list(APPEND CMAKE_FIND_ROOT_PATH "${JPLACER_WINDOWS_SDK}" "${JPLACER_WINDOWS_DEPS}" "${JPLACER_WINDOWS_OPENCV}")
set(OpenCV_DIR "${JPLACER_WINDOWS_OPENCV}/lib/cmake/opencv4" CACHE PATH "The OpenCV packaging/build-opencv.sh built")
