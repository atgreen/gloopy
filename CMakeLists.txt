# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only

cmake_minimum_required(VERSION 3.22)

project(Gloopy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Pull JUCE via FetchContent (pinned to the latest stable release).
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Enlarge the LV2 host's per-port atom buffer (default 8 KB) so plugins with a
# large state (e.g. ZynAddSubFX) don't overflow it and spam "out of space".
# LV2 hosting + this `sed` patch are POSIX-only; skip on Windows (no LV2 host there).
set(_gloopy_lv2impl "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/juce_LV2PluginFormatImpl.h")
if(NOT WIN32 AND EXISTS "${_gloopy_lv2impl}")
    execute_process(COMMAND sed -i
        "s/sequenceSize = 8192/sequenceSize = 1048576/;s/initialBufferSize = 8192/initialBufferSize = 1048576/"
        "${_gloopy_lv2impl}")
endif()

# ---------------------------------------------------------------------------
# The application target.
# ---------------------------------------------------------------------------
juce_add_gui_app(Gloopy
    PRODUCT_NAME "gloopy"          # shipped executable is lowercase `gloopy`
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
    Source/ControlGroups.cpp
    Source/Buses.cpp
    Source/Scales.cpp
    Source/Modulation.cpp
    Source/Loudness.cpp
    Source/Diagnostics.cpp
    Source/Tempo.cpp
    Source/Notes.cpp
    Source/Git.cpp
    Source/Controllers.cpp
    Source/Waveform.cpp
    Source/ArrangeView.cpp
    Source/PianoRoll.cpp
    Source/MixerView.cpp
    Source/GloopyLookAndFeel.cpp
    Source/SurgeGenerator.cpp)   # Surge glue; PIMPL stub when GLOOPY_WITH_SURGE is off

# Plugin-host formats by platform: VST3 everywhere; LV2 on Linux/macOS only
# (LV2 hosting is niche on Windows, and its build path pulls in POSIX-isms).
if(WIN32)
    set(_gloopy_host_lv2 0)
else()
    set(_gloopy_host_lv2 1)
endif()
target_compile_definitions(Gloopy PUBLIC
    JUCE_WEB_BROWSER=0            # no webkit dependency
    JUCE_USE_CURL=0              # no libcurl dependency
    JUCE_PLUGINHOST_VST3=1        # host VST3 plugins (all platforms)
    JUCE_PLUGINHOST_LV2=${_gloopy_host_lv2}   # host LV2 plugins (Linux/macOS)
    JUCE_PLUGINHOST_AU=0
    JUCE_APPLICATION_NAME_STRING="Gloopy"        # display name stays capitalised
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Gloopy,JUCE_VERSION>"
    GLOOPY_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/third_party")   # vendored assets (bundled piano)

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
if(WIN32)
    # Windows (CI): gRPC + protobuf come from vcpkg as CMake config packages, not
    # pkg-config. protoc + the grpc_cpp_plugin are imported executable targets.
    # NOTE: this branch is exercised by the Windows CI workflow (.github/workflows/windows.yml)
    # and is not built on Linux/macOS, which keep the pkg-config path below.
    find_package(protobuf CONFIG REQUIRED)
    find_package(gRPC     CONFIG REQUIRED)
    set(GLOOPY_PROTOC      $<TARGET_FILE:protobuf::protoc>)
    set(GLOOPY_GRPC_PLUGIN $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
    set(GLOOPY_GRPC_LIBS   gRPC::grpc++ protobuf::libprotobuf)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPCPP   REQUIRED IMPORTED_TARGET grpc++)
    pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
    find_program(GLOOPY_PROTOC       protoc          HINTS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
    find_program(GLOOPY_GRPC_PLUGIN  grpc_cpp_plugin HINTS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
    set(GLOOPY_GRPC_LIBS PkgConfig::GRPCPP PkgConfig::PROTOBUF)
endif()

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
target_link_libraries(Gloopy PRIVATE ${GLOOPY_GRPC_LIBS})

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
# Vendored Surge XT synth engine (GPL-3.0) — the default instrument voice.
# Embedded like sfizz, but scoped to surge-common (the synth core): no JUCE,
# no plugin/standalone/FX/tests. surge-common builds as C++20 at its OWN
# directory scope (it does not propagate the standard), so Gloopy stays C++17;
# only the future SurgeGenerator TU (slice 3) compiles as C++20 + -fno-char8_t.
# GPL-3.0 combines with Gloopy's AGPL-3.0 (GPLv3/AGPLv3 §13). Toggle off for a
# lean, pure-AGPL, C++17-only build with -DGLOOPY_WITH_SURGE=OFF.
# See docs/surge-embed.md for the proven recipe and the six-slice plan.
# ---------------------------------------------------------------------------
option(GLOOPY_WITH_SURGE "Embed Surge XT as the default synth (GPL-3.0)" ON)
set(GLOOPY_SURGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/surge"
    CACHE PATH "Surge XT source tree (the third_party/surge submodule by default)")
if(GLOOPY_WITH_SURGE AND EXISTS "${GLOOPY_SURGE_DIR}/src/common/CMakeLists.txt")
    set(SURGE_SKIP_JUCE_FOR_RACK TRUE CACHE BOOL "" FORCE)   # synth core only, no JUCE UI
    set(SURGE_SKIP_VST3          TRUE CACHE BOOL "" FORCE)
    set(SURGE_SKIP_STANDALONE    TRUE CACHE BOOL "" FORCE)
    set(SURGE_BUILD_XT           OFF  CACHE BOOL "" FORCE)
    set(SURGE_BUILD_FX           OFF  CACHE BOOL "" FORCE)
    set(SURGE_BUILD_CLAP         OFF  CACHE BOOL "" FORCE)
    set(SURGE_BUILD_LV2          OFF  CACHE BOOL "" FORCE)
    set(SURGE_BUILD_TESTRUNNER   OFF  CACHE BOOL "" FORCE)
    set(SURGE_SKIP_WERROR        TRUE CACHE BOOL "" FORCE)   # vendored sqlite 3.23.3 warns on GCC 16
    set(BUILD_TESTING            OFF  CACHE BOOL "" FORCE)
    # Embedding compat shim: surge-common references r8brain via ${CMAKE_SOURCE_DIR}
    # (the top-level project = Gloopy when embedded), which mis-resolves. Redirect
    # those two lines to ${SURGE_SOURCE_DIR} (Surge's own root, correct when embedded).
    # Idempotent; mirrors the JUCE LV2 sed above. See docs/surge-embed.md.
    execute_process(COMMAND sed -i
        "s|\${CMAKE_SOURCE_DIR}/libs/r8brain-free-src|\${SURGE_SOURCE_DIR}/libs/r8brain-free-src|g"
        "${GLOOPY_SURGE_DIR}/src/common/CMakeLists.txt")
    add_subdirectory(${GLOOPY_SURGE_DIR} ${CMAKE_BINARY_DIR}/surge EXCLUDE_FROM_ALL)
    target_link_libraries(Gloopy PRIVATE surge::surge-common)
    target_compile_definitions(Gloopy PRIVATE GLOOPY_WITH_SURGE=1)
    # SurgeGenerator.cpp is the only TU that includes Surge headers: build it as
    # C++20 + -fno-char8_t (Surge's requirement; sst-effects headers need C++20).
    # -w silences warnings from the heavy third-party header instantiation.
    set_source_files_properties(Source/SurgeGenerator.cpp PROPERTIES
        COMPILE_OPTIONS "-std=c++20;-fno-char8_t;-w")
    message(STATUS "Gloopy: Surge XT synth engine ENABLED (${GLOOPY_SURGE_DIR})")
elseif(GLOOPY_WITH_SURGE)
    message(WARNING "GLOOPY_WITH_SURGE=ON but no Surge source at ${GLOOPY_SURGE_DIR}. "
                    "Init the third_party/surge submodule or pass -DGLOOPY_SURGE_DIR=<path>. "
                    "Building WITHOUT Surge for now.")
endif()

# ---------------------------------------------------------------------------
# Unit tests — a small headless console target over the pure logic
# (NoteScheduler / swing, ValueTree serialisation). Built with -DGLOOPY_TESTS=ON.
# Kept out of the default build so it doesn't slow the app iteration loop.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Install rules. The exe-adjacent asset fallback in findPianoSfz() looks for
# <exeDir>/assets/SalamanderGrandPiano/..., so lay the samples down there. The
# GUI app artefact lands in bin/, and the vendored piano beside it in bin/assets.
# ---------------------------------------------------------------------------
install(TARGETS Gloopy
    RUNTIME DESTINATION bin
    BUNDLE  DESTINATION .)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SalamanderGrandPiano
    DESTINATION bin/assets)
# Bundled Surge factory content (patches + wavetables) for the Presets tab / factory
# sounds out of the box; installed beside the binary and found by SurgeGenerator::dataDir().
if(GLOOPY_WITH_SURGE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/surge-data")
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/surge-data
        DESTINATION bin/assets)
endif()
# Bundled Surge XT LV2 plugin (built by scripts/build-surge-plugin.sh) — shipped beside the
# binary so "+ Synth -> Surge XT (full editor)" works with no external install; PluginHost
# adds <exeDir>/plugins to its scan path. The vendored bundle is a Linux .lv2; skip on
# Windows, which hosts VST3 only (a Windows Surge VST3 would be bundled by the CI build).
if(NOT WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/surge-plugin")
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/surge-plugin/
        DESTINATION bin/plugins)
endif()

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
        juce::juce_graphics          # juce::Colour, used by Clip.h (SessionModel.h) / Track model
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME GloopyUnitTests COMMAND GloopyTests)
endif()

# ---------------------------------------------------------------------------
# Packaging (CPack) — `cpack -G RPM` / `cpack -G DEB` build native packages from
# the install() rules above. Driven by .github/workflows/release.yaml on a tag.
# Shared-library dependencies are auto-detected (RPM AUTOREQ / DEB SHLIBDEPS).
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "gloopy")
set(CPACK_PACKAGE_VENDOR "atgreen")
set(CPACK_PACKAGE_CONTACT "Anthony Green <green@moxielogic.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A scriptable, composition-as-repo DAW")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/atgreen/gloopy")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")   # -> /usr/bin/gloopy, /usr/bin/assets/...
set(CPACK_STRIP_FILES ON)
# With Surge embedded the binary is a combined AGPL-3.0 / GPL-3.0 work.
if(GLOOPY_WITH_SURGE)
    set(_gloopy_license "AGPL-3.0-only AND GPL-3.0-or-later")
else()
    set(_gloopy_license "AGPL-3.0-only")
endif()
# RPM
set(CPACK_RPM_PACKAGE_LICENSE "${_gloopy_license}")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
# DEB
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
include(CPack)
