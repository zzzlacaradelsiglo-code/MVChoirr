cmake_minimum_required(VERSION 3.22)

# Runtime de Visual C++ enlazado de forma estatica: el .vst3 no necesita instalar nada extra
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(MVChoirLab VERSION 2.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Logo y arte de la interfaz incrustados en el plugin
juce_add_binary_data(MVAssets SOURCES
    Assets/logo.png
    Assets/art.jpg)

# Es un EFECTO de audio (se carga en un inserto del mezclador), no un instrumento
juce_add_plugin(MVChoirLab
    COMPANY_NAME "Manana Vemos"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Mvlb
    PLUGIN_CODE Mvc2
    FORMATS VST3
    PRODUCT_NAME "MV CHOIR LAB"
    VST3_CATEGORIES Fx)

target_sources(MVChoirLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(MVChoirLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MVChoirLab
    PRIVATE
        MVAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
