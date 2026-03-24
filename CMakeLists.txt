cmake_minimum_required(VERSION 3.22)

project(CleanVowelSynth VERSION 0.0.1)

# Set this to your JUCE checkout if you are not vendoring JUCE inside this project.
# Example: cmake -B build -DJUCE_DIR=/home/you/JUCE
if (DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    message(FATAL_ERROR "JUCE not found. Either place JUCE in ./JUCE or pass -DJUCE_DIR=/path/to/JUCE")
endif()

juce_add_plugin(CleanVowelSynth
    COMPANY_NAME "OpenAIUser"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE OAIU
    PLUGIN_CODE Cvwl
    FORMATS VST3
    PRODUCT_NAME "CleanVowelSynth"
    VST3_CATEGORIES Instrument|Synth
)

juce_generate_juce_header(CleanVowelSynth)

target_sources(CleanVowelSynth PRIVATE
    source/PluginProcessor.cpp
    source/PluginProcessor.h
    source/PluginEditor.cpp
    source/PluginEditor.h
)

target_compile_features(CleanVowelSynth PRIVATE cxx_std_17)

target_link_libraries(CleanVowelSynth PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
)

target_compile_definitions(CleanVowelSynth PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
