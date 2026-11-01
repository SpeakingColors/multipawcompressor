cmake_minimum_required(VERSION 3.22)
# The release workflow passes the tag as MP_VERSION so menu, installer and release agree.
set(MP_VERSION "0.1.0" CACHE STRING "Plugin version")
project(MultiPawCompressor VERSION ${MP_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

include(FetchContent)
if(MP_JUCE_DIR)
    add_subdirectory(${MP_JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.16-security
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(MultiPawAssets SOURCES
    assets/fonts/BarlowCondensed-Medium.ttf
    assets/fonts/BarlowCondensed-SemiBold.ttf
    assets/fonts/BarlowCondensed-Bold.ttf
    assets/fonts/BarlowCondensed-ExtraBoldItalic.ttf
    assets/fonts/JetBrainsMono-Regular.ttf)
set_target_properties(MultiPawAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(MultiPawCompressor
    COMPANY_NAME "Studio 87"
    PLUGIN_MANUFACTURER_CODE Stu8
    PLUGIN_CODE MPaw
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "MultiPaw Compressor")

set(MP_SOURCES
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
    src/plugin/ParamTable.h
    src/plugin/ParamMap.h
    src/core/MultiPawCore.h
    src/editor/PluginEditor.h)
target_sources(MultiPawCompressor PRIVATE ${MP_SOURCES})

target_compile_definitions(MultiPawCompressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MultiPawCompressor
    PRIVATE MultiPawAssets juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
