cmake_minimum_required(VERSION 3.22)
project(IvoryDrive VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IVORY_BUILD_PLUGIN "Build the native VST3 effect" ON)
option(IVORY_BUILD_TESTS "Build DSP and processor validation" ON)
option(IVORY_LINUX_VALIDATION "Allow a Linux build for development validation only" OFF)
option(IVORY_SANITIZE "Enable AddressSanitizer and UBSan on the independent DSP tests" OFF)
set(IVORY_JUCE_PATH "" CACHE PATH "Optional existing JUCE 8.0.10 checkout")

if(IVORY_BUILD_TESTS)
    enable_testing()
    add_executable(IvoryDspTests Tests/DspTests.cpp)
    target_include_directories(IvoryDspTests PRIVATE Source)
    if(MSVC)
        target_compile_options(IvoryDspTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(IvoryDspTests PRIVATE -Wall -Wextra -Wpedantic)
        if(IVORY_SANITIZE)
            target_compile_options(IvoryDspTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(IvoryDspTests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME independent_dsp COMMAND IvoryDspTests)
    set_tests_properties(independent_dsp PROPERTIES TIMEOUT 180)
endif()

if(IVORY_BUILD_PLUGIN)
    if(NOT WIN32 AND NOT IVORY_LINUX_VALIDATION)
        message(FATAL_ERROR "The release target is Windows x64. For local DSP tests use -DIVORY_BUILD_PLUGIN=OFF; for a development-only Linux plug-in build add -DIVORY_LINUX_VALIDATION=ON.")
    endif()
    if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Ivory Drive requires a 64-bit target.")
    endif()
    if(IVORY_JUCE_PATH)
        add_subdirectory("${IVORY_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 3af3ce009f6a02f6fa651008fffb5b41743a9fab
            GIT_PROGRESS TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(IvoryDrive
        COMPANY_NAME "Ivory Audio"
        BUNDLE_ID "audio.ivory.drive"
        PLUGIN_MANUFACTURER_CODE Ivry
        PLUGIN_CODE IvDr
        FORMATS VST3
        PRODUCT_NAME "Ivory Drive"
        DESCRIPTION "Independent pre/post emphasis harmonic saturation"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Distortion)
    juce_generate_juce_header(IvoryDrive)
    target_sources(IvoryDrive PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h)
    target_include_directories(IvoryDrive PUBLIC Source)
    target_compile_definitions(IvoryDrive PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(IvoryDrive PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(IvoryDrive PRIVATE /utf-8)
    endif()

    if(IVORY_BUILD_TESTS)
        add_executable(IvoryPluginTests Tests/PluginTests.cpp)
        target_include_directories(IvoryPluginTests PRIVATE
            $<TARGET_PROPERTY:IvoryDrive,INCLUDE_DIRECTORIES>)
        target_link_libraries(IvoryPluginTests PRIVATE IvoryDrive)
        add_test(NAME plugin_integration COMMAND IvoryPluginTests)
        set_tests_properties(plugin_integration PROPERTIES TIMEOUT 180)
    endif()
endif()
