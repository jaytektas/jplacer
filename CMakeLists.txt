cmake_minimum_required(VERSION 3.16)
project(jplacer VERSION 0.1.11 LANGUAGES CXX)

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
file(GLOB JPLACER_MODEL_SRC   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.cpp)
file(GLOB JPLACER_TASKS_SRC   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/tasks/*.cpp)
file(GLOB JPLACER_SETUP_SRC   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/setup/*.cpp)
file(GLOB JPLACER_UI_SRC      CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)
file(GLOB JPLACER_APP_SRC     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)

add_library(jplacer_common STATIC ${JPLACER_COMMON_SRC})
target_include_directories(jplacer_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(jplacer_common PUBLIC JFramework::platform)

# Controllers, links, firmware profiles. No widget header is included here.
add_library(jplacer_machine STATIC ${JPLACER_MACHINE_SRC})
target_link_libraries(jplacer_machine PUBLIC jplacer_common Threads::Threads OpenSSL::SSL OpenSSL::Crypto)

# Reading OpenPnP's files (machine.xml) into jplacer's. Expat parses the XML.
find_package(EXPAT REQUIRED)
add_library(jplacer_openpnp STATIC ${JPLACER_OPENPNP_SRC})
target_link_libraries(jplacer_openpnp PUBLIC jplacer_machine PRIVATE EXPAT::EXPAT)

# Cameras: capture devices (V4L2 on Linux) and simulated ones, a thread
# each, frames as RGBA. libjpeg(-turbo) decodes MJPG.
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
add_library(jplacer_camera STATIC ${JPLACER_CAMERA_SRC})
target_link_libraries(jplacer_camera PUBLIC jplacer_machine PRIVATE jplacer_openpnp JPEG::JPEG PNG::PNG)

# Measuring pictures: finding marks, to a fraction of a pixel, with nothing to tune.
add_library(jplacer_vision STATIC ${JPLACER_VISION_SRC})
target_link_libraries(jplacer_vision PUBLIC jplacer_common)

# OpenPnP's vision pipelines (CvPipeline) and their stages, on OpenCV: the
# static build packaging/build-opencv.sh makes (only the modules the stages
# use, nothing else to ship). jplacer's own finders above need none of it.
set(OpenCV_DIR "$ENV{HOME}/opencv-jplacer/lib/cmake/opencv4" CACHE PATH "The OpenCV packaging/build-opencv.sh built")
find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc calib3d features2d flann objdetect)
file(GLOB JPLACER_PIPELINE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/*.cpp)
add_library(jplacer_pipeline STATIC ${JPLACER_PIPELINE_SRC})
target_link_libraries(jplacer_pipeline PUBLIC jplacer_openpnp jplacer_model jplacer_camera jplacer_vision ${OpenCV_LIBS})
target_include_directories(jplacer_pipeline PUBLIC ${OpenCV_INCLUDE_DIRS})

# OpenPnP's model: parts, packages and footprints, boards, panels and jobs,
# kept in OpenPnP's own files. No GUI, no hardware.
add_library(jplacer_model STATIC ${JPLACER_MODEL_SRC})
target_link_libraries(jplacer_model PUBLIC jplacer_openpnp)
# Procedures that drive the machine, its cameras and vision together:
# calibrating a camera, the visual homing test, finding a board.
add_library(jplacer_tasks STATIC ${JPLACER_TASKS_SRC})
target_link_libraries(jplacer_tasks PUBLIC jplacer_machine jplacer_camera jplacer_vision jplacer_model jplacer_pipeline jplacer_setup)

# Panels and views over the other modules: the only module with widgets.
# Machine Setup's model of a cell: its tree, what each part edits, adding
# and removing parts. No widget is made here.
add_library(jplacer_setup STATIC ${JPLACER_SETUP_SRC})
target_link_libraries(jplacer_setup PUBLIC jplacer_machine jplacer_model jplacer_pipeline)

add_library(jplacer_ui STATIC ${JPLACER_UI_SRC})
target_link_libraries(jplacer_ui PUBLIC jplacer_machine jplacer_camera jplacer_setup jplacer_model jplacer_pipeline)
target_include_directories(jplacer_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg)
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
target_link_libraries(jplacer PRIVATE jplacer_ui jplacer_setup jplacer_tasks jplacer_model jplacer_vision jplacer_camera jplacer_openpnp jplacer_machine jplacer_common JFramework::platform
                                      OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_precompile_headers(jplacer PRIVATE ${JPLACER_PCH})

# TESTS: one executable per test file under tests/<module>/, run by ctest.
option(JPLACER_TESTS "Build jplacer's tests" ON)
if(JPLACER_TESTS)
    enable_testing()
    foreach(module machine openpnp camera vision pipeline model tasks setup)
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
    # The NeoDen 4 cameras' test loads a stand-in for the machine's camera library, as the real one is loaded.
    add_library(neodencam SHARED ${CMAKE_CURRENT_SOURCE_DIR}/tests/fakes/neodencam.cpp)
    add_dependencies(test_neoden4_camera neodencam)
    set_tests_properties(test_neoden4_camera PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:neodencam>")
    # The import's test reads an imported SimulatedUpCamera through the camera module.
    target_link_libraries(test_openpnp_import PRIVATE jplacer_camera)
    # The calibration fit's test also checks the machine module's calibration record.
    target_link_libraries(test_calibration_fit PRIVATE jplacer_machine)
    # The simulated scene's test runs the camera's simulation into the finder.
    target_link_libraries(test_simulated_scene PRIVATE jplacer_camera)
endif()
