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
    Source/ChannelRackView.cpp
    Source/PianoRoll.cpp
    Source/PlaylistView.cpp
    Source/MixerView.cpp)

target_compile_definitions(Gloopy PRIVATE
    JUCE_WEB_BROWSER=0            # no webkit dependency
    JUCE_USE_CURL=0              # no libcurl dependency
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Gloopy,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Gloopy,JUCE_VERSION>")

target_link_libraries(Gloopy PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
