cmake_minimum_required(VERSION 3.22)
project(DopeBeat VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Runtime estatico: el .vst3 no necesita instalar VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DopeBeat
    COMPANY_NAME "ICONICO"
    PLUGIN_MANUFACTURER_CODE Icnc
    PLUGIN_CODE Dpbt
    FORMATS VST3
    PRODUCT_NAME "DOPE BEAT"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx)

juce_add_binary_data(DopeBeatAssets SOURCES Assets/GBET.png)

target_sources(DopeBeat PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DopeBeat PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(DopeBeat PRIVATE /utf-8 /bigobj)
endif()

target_link_libraries(DopeBeat
    PRIVATE
        DopeBeatAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

juce_generate_juce_header(DopeBeat)
