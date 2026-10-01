cmake_minimum_required(VERSION 3.16)
project(jplacer VERSION 0.1.0 LANGUAGES CXX)

# A BETA is the next version with a pre-release after it: set this to "beta.1"
# and 0.2.0 ships as 0.2.0-beta.1, which orders before 0.2.0 itself. Empty for
# a full release. packaging/build-release.sh reads both lines, and publishes a
# version with a pre-release as a GitHub pre-release -- which is what keeps it
# away from everyone but the people who ticked "Include beta versions".
set(JPLACER_PRERELEASE "")
if(JPLACER_PRERELEASE)
    set(JPLACER_FULL_VERSION "${PROJECT_VERSION}-${JPLACER_PRERELEASE}")
else()
    set(JPLACER_FULL_VERSION "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
find_package(JFramework 0.1 CONFIG REQUIRED)
# libj_platform.a is a static archive, so it exports none of its own private
# dependencies. The updater talks HTTPS through JHttpClient, so OpenSSL is a
# hard dependency here rather than an optional one.
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

file(GLOB JPLACER_APP_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)

# PRECOMPILED HEADERS. The framework's core header pulls hundreds of files
# behind it; without this every translation unit pays for them again.
set(JPLACER_PCH <j/core/Log.h> <j/core/Signal.h> <j/core/JWidget.h> <j/core/JStyle.h>
                <string> <vector> <memory> <functional> <algorithm>)

add_executable(jplacer ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp ${JPLACER_APP_SRC})
target_include_directories(jplacer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# One source of truth for the version: CMake's, handed to the code rather than
# restated in a header where the two could drift. The updater compares it with
# the newest release's tag.
target_compile_definitions(jplacer PRIVATE JPLACER_VERSION="${JPLACER_FULL_VERSION}")
target_link_libraries(jplacer PRIVATE JFramework::platform
                                      OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_precompile_headers(jplacer PRIVATE ${JPLACER_PCH})
