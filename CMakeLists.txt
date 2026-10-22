cmake_minimum_required(VERSION 3.22)

# Universal Binary (Apple Silicon + Intel), ab macOS 11
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

project(CrystalVoice VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CV_BUILD_PLUGIN "AU/VST3-Plugin bauen" ON)
option(CV_BUILD_SIM "Simulation/Messung bauen" ON)
option(CV_BUILD_SMOKE "Rauchtest des Plugins (Host-Simulation) bauen" OFF)

# DSP-Kern ohne JUCE-Abhängigkeit
add_library(cv_core STATIC core/Aec.cpp core/Engine.cpp core/VocalModel.cpp core/IrMeasure.cpp)
target_include_directories(cv_core PUBLIC core)
set_target_properties(cv_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CV_BUILD_SIM)
    add_executable(cv_sim tests/sim.cpp)
    target_link_libraries(cv_sim PRIVATE cv_core)
    add_executable(cv_irtest tests/ir_test.cpp)
    target_link_libraries(cv_irtest PRIVATE cv_core)
endif()

if(CV_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)

    juce_add_plugin(CrystalVoice
        COMPANY_NAME "STUDIO KAUTZ"
        BUNDLE_ID de.studiokautz.crystalvoice
        PLUGIN_MANUFACTURER_CODE Kutz
        PLUGIN_CODE Crv1
        FORMATS AU VST3
        PRODUCT_NAME "Crystal Voice"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Restoration
        COPY_PLUGIN_AFTER_BUILD TRUE)

    target_sources(CrystalVoice PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)

    # Schriften (SIL OFL, siehe Resources/licenses) und Logo
    juce_add_binary_data(CrystalVoiceData SOURCES
        Resources/BarlowCondensed-SemiBold.ttf
        Resources/BarlowCondensed-Bold.ttf
        Resources/logo_print.png)
    set_target_properties(CrystalVoiceData PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_compile_definitions(CrystalVoice PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        $<$<PLATFORM_ID:Linux>:JUCE_ALSA=0>
        $<$<PLATFORM_ID:Linux>:JUCE_JACK=0>)

    target_link_libraries(CrystalVoice
        PRIVATE cv_core CrystalVoiceData juce::juce_audio_utils juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

    if(CV_BUILD_SMOKE)
        add_executable(cv_smoke tests/plugin_smoke.cpp)
        target_link_libraries(cv_smoke PRIVATE CrystalVoice)
        target_include_directories(cv_smoke PRIVATE $<TARGET_PROPERTY:CrystalVoice,INCLUDE_DIRECTORIES>)
        target_compile_definitions(cv_smoke PRIVATE $<TARGET_PROPERTY:CrystalVoice,COMPILE_DEFINITIONS>)

        add_executable(cv_snapshot tests/editor_snapshot.cpp)
        target_link_libraries(cv_snapshot PRIVATE CrystalVoice)
        target_include_directories(cv_snapshot PRIVATE $<TARGET_PROPERTY:CrystalVoice,INCLUDE_DIRECTORIES>)
        target_compile_definitions(cv_snapshot PRIVATE $<TARGET_PROPERTY:CrystalVoice,COMPILE_DEFINITIONS>)
    endif()
endif()
