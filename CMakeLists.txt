# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only

cmake_minimum_required(VERSION 3.22)

project(Gloopy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Pull JUCE via FetchContent (pinned to a stable 8.x release).
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Enlarge the LV2 host's per-port atom buffer (default 8 KB) so plugins with a
# large state (e.g. ZynAddSubFX) don't overflow it and spam "out of space".
set(_gloopy_lv2impl "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/juce_LV2PluginFormatImpl.h")
if(EXISTS "${_gloopy_lv2impl}")
    execute_process(COMMAND sed -i
        "s/sequenceSize = 8192/sequenceSize = 1048576/;s/initialBufferSize = 8192/initialBufferSize = 1048576/"
        "${_gloopy_lv2impl}")
endif()

# ---------------------------------------------------------------------------
# The application target.
# ---------------------------------------------------------------------------
juce_add_gui_app(Gloopy
    PRODUCT_NAME "Gloopy"
    COMPANY_NAME "atgreen")

juce_generate_juce_header(Gloopy)

target_sources(Gloopy PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Composition.cpp
    Source/Recording.cpp
    Source/Presets.cpp
    Source/Parameters.cpp
    Source/Locations.cpp
    Source/Exports.cpp
    Source/Cli.cpp
    Source/Midi.cpp
    Source/ClipOps.cpp
    Source/MixerScenes.cpp
    Source/Buses.cpp
    Source/ArrangeView.cpp
    Source/PianoRoll.cpp
    Source/MixerView.cpp
    Source/GloopyLookAndFeel.cpp)

target_compile_definitions(Gloopy PUBLIC
    JUCE_WEB_BROWSER=0            # no webkit dependency
    JUCE_USE_CURL=0              # no libcurl dependency
    JUCE_PLUGINHOST_VST3=1        # host VST3 plugins
    JUCE_PLUGINHOST_LV2=1         # host LV2 plugins
    JUCE_PLUGINHOST_AU=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Gloopy,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Gloopy,JUCE_VERSION>")

target_link_libraries(Gloopy PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_osc
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)   # LTO omitted: much faster builds

# ---------------------------------------------------------------------------
# gRPC control API — generate C++ from proto/gloopy.proto and link.
# Use Fedora's protoc/grpc_cpp_plugin (matches the system libprotobuf), NOT a
# stray protoc from PATH, to avoid codegen/runtime version mismatches.
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP   REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
find_program(GLOOPY_PROTOC       protoc          HINTS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
find_program(GLOOPY_GRPC_PLUGIN  grpc_cpp_plugin HINTS /usr/bin /usr/local/bin NO_DEFAULT_PATH)

set(GLOOPY_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/proto/gloopy.proto)
set(GLOOPY_GEN   ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GLOOPY_GEN})
add_custom_command(
    OUTPUT  ${GLOOPY_GEN}/gloopy.pb.cc ${GLOOPY_GEN}/gloopy.pb.h
            ${GLOOPY_GEN}/gloopy.grpc.pb.cc ${GLOOPY_GEN}/gloopy.grpc.pb.h
    COMMAND ${GLOOPY_PROTOC}
            --experimental_allow_proto3_optional
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
            --cpp_out=${GLOOPY_GEN}
            --grpc_out=${GLOOPY_GEN}
            --plugin=protoc-gen-grpc=${GLOOPY_GRPC_PLUGIN}
            ${GLOOPY_PROTO}
    DEPENDS ${GLOOPY_PROTO}
    VERBATIM)

target_sources(Gloopy PRIVATE
    ${GLOOPY_GEN}/gloopy.pb.cc
    ${GLOOPY_GEN}/gloopy.grpc.pb.cc
    Source/GrpcServer.cpp)
target_include_directories(Gloopy PRIVATE ${GLOOPY_GEN})
target_link_libraries(Gloopy PRIVATE PkgConfig::GRPCPP PkgConfig::PROTOBUF)

# ---------------------------------------------------------------------------
# Vendored sfizz (third_party/sfizz) — the native SFZ engine, built as a static
# library and linked directly (no plugin hosting, no state hack). Library only:
# plugins/JACK/render/tests off, dr_libs audio backend (no libsndfile LGPL),
# submodule check off (we copied the source in, there's no .git).
# ---------------------------------------------------------------------------
set(SFIZZ_SHARED              OFF CACHE BOOL "" FORCE)
set(SFIZZ_JACK                OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER              OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS               OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS               OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS          OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS            OFF CACHE BOOL "" FORCE)
set(SFIZZ_USE_SNDFILE         OFF CACHE BOOL "" FORCE)
set(SFIZZ_GIT_SUBMODULE_CHECK OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO                OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/sfizz EXCLUDE_FROM_ALL)
target_link_libraries(Gloopy PRIVATE sfizz::sfizz)
target_include_directories(Gloopy PRIVATE third_party/sfizz/src)

# ---------------------------------------------------------------------------
# Unit tests — a small headless console target over the pure logic
# (NoteScheduler / swing, ValueTree serialisation). Built with -DGLOOPY_TESTS=ON.
# Kept out of the default build so it doesn't slow the app iteration loop.
# ---------------------------------------------------------------------------
option(GLOOPY_TESTS "Build the Gloopy unit-test target" OFF)
if(GLOOPY_TESTS)
    enable_testing()
    juce_add_console_app(GloopyTests PRODUCT_NAME "GloopyTests")
    juce_generate_juce_header(GloopyTests)
    target_sources(GloopyTests PRIVATE tests/TestMain.cpp)
    target_include_directories(GloopyTests PRIVATE Source)
    target_link_libraries(GloopyTests PRIVATE
        juce::juce_audio_basics
        juce::juce_data_structures
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME GloopyUnitTests COMMAND GloopyTests)
endif()
