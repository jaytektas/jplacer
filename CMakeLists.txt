cmake_minimum_required(VERSION 3.16)
project(jplacer VERSION 0.1.0 LANGUAGES CXX)

# THE VERSION is the line above: the last release, raised by packaging/build-release.sh as it cuts the
# next one. A BETA is never written here -- packaging/build-beta.sh builds one as <next patch>-beta.N by
# passing JPLACER_VERSION_OVERRIDE, and puts the build back to the plain version afterwards, so a later
# build cannot say beta by accident.
set(JPLACER_VERSION_OVERRIDE "" CACHE STRING "Build as this version instead of PROJECT_VERSION (betas)")
if(JPLACER_VERSION_OVERRIDE)
    set(JPLACER_FULL_VERSION "${JPLACER_VERSION_OVERRIDE}")
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

# MODULES. Each subsystem is a static library of its own, built from small
# translation units, so a change recompiles its module and relinks, nothing
# more. A module links only the modules it uses: machine code never sees the
# GUI, the app shell wires modules together.
file(GLOB JPLACER_COMMON_SRC  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp)
file(GLOB JPLACER_MACHINE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/machine/*.cpp)
file(GLOB JPLACER_OPENPNP_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/openpnp/*.cpp)
file(GLOB JPLACER_CAMERA_SRC  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/camera/*.cpp)
file(GLOB JPLACER_VISION_SRC  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/vision/*.cpp)
file(GLOB JPLACER_UI_SRC      CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)
file(GLOB JPLACER_APP_SRC     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)

add_library(jplacer_common STATIC ${JPLACER_COMMON_SRC})
target_include_directories(jplacer_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(jplacer_common PUBLIC JFramework::platform)

# Controllers, links, firmware profiles. No widget header is included here.
add_library(jplacer_machine STATIC ${JPLACER_MACHINE_SRC})
target_link_libraries(jplacer_machine PUBLIC jplacer_common Threads::Threads)

# Reading OpenPnP's files (machine.xml) into jplacer's. Expat parses the XML.
find_package(EXPAT REQUIRED)
add_library(jplacer_openpnp STATIC ${JPLACER_OPENPNP_SRC})
target_link_libraries(jplacer_openpnp PUBLIC jplacer_machine PRIVATE EXPAT::EXPAT)

# Cameras: capture devices (V4L2 on Linux) and simulated ones, a thread
# each, frames as RGBA. libjpeg(-turbo) decodes MJPG.
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
add_library(jplacer_camera STATIC ${JPLACER_CAMERA_SRC})
target_link_libraries(jplacer_camera PUBLIC jplacer_machine PRIVATE JPEG::JPEG PNG::PNG)

# Measuring pictures: finding marks, to a fraction of a pixel, with nothing to tune.
add_library(jplacer_vision STATIC ${JPLACER_VISION_SRC})
target_link_libraries(jplacer_vision PUBLIC jplacer_common)

# Panels and views over the other modules: the only module with widgets.
add_library(jplacer_ui STATIC ${JPLACER_UI_SRC})
target_link_libraries(jplacer_ui PUBLIC jplacer_machine jplacer_camera)
target_precompile_headers(jplacer_ui PRIVATE <j/core/JWidget.h> <j/core/JStyle.h> <j/core/JContainer.h>)

# PRECOMPILED HEADERS. The framework's core header pulls hundreds of files
# behind it; without this every translation unit pays for them again.
set(JPLACER_PCH <j/core/Log.h> <j/core/Signal.h> <j/core/JWidget.h> <j/core/JStyle.h>
                <string> <vector> <memory> <functional> <algorithm>)

add_executable(jplacer ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp ${JPLACER_APP_SRC})
# One source of truth for the version: CMake's, handed to the code rather than
# restated in a header where the two could drift. The updater compares it with
# the newest release's tag.
target_compile_definitions(jplacer PRIVATE JPLACER_VERSION="${JPLACER_FULL_VERSION}")
target_link_libraries(jplacer PRIVATE jplacer_ui jplacer_camera jplacer_openpnp jplacer_machine jplacer_common JFramework::platform
                                      OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_precompile_headers(jplacer PRIVATE ${JPLACER_PCH})

# TESTS: one executable per test file under tests/<module>/, run by ctest.
option(JPLACER_TESTS "Build jplacer's tests" ON)
if(JPLACER_TESTS)
    enable_testing()
    foreach(module machine openpnp camera vision)
      file(GLOB tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${module}/*.cpp)
      foreach(src ${tests})
        get_filename_component(name ${src} NAME_WE)
        add_executable(${name} ${src})
        target_link_libraries(${name} PRIVATE jplacer_${module})
        target_compile_definitions(${name} PRIVATE JPLACER_PROFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles")
        target_compile_definitions(${name} PRIVATE JPLACER_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME ${name} COMMAND ${name})
      endforeach()
    endforeach()
    # The calibration fit's test also checks the machine module's calibration record.
    target_link_libraries(test_calibration_fit PRIVATE jplacer_machine)
endif()
