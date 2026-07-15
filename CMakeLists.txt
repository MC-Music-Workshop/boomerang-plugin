cmake_minimum_required(VERSION 3.22)

project(Boomerang VERSION 2.0.0)

# ==============================================================================
# Version from Git (Single Source of Truth)
# ==============================================================================
# At a tag:      v2.0.0-beta-3 -> 2.0.0-beta-3
# After a tag:   v2.0.0-beta-3-5-g1234abc -> 2.0.0-beta-3+5.g1234abc
# No tags:       -> 2.0.0-dev

execute_process(
    COMMAND git describe --tags --always
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
)

if(GIT_RESULT EQUAL 0 AND GIT_DESCRIBE)
    # Strip 'v' prefix if present
    string(REGEX REPLACE "^v" "" VERSION_STRING "${GIT_DESCRIBE}")
    # Convert git describe format (2.0.0-beta-3-5-g1234abc) to semver metadata format (2.0.0-beta-3+5.g1234abc)
    # Match: base-N-gHASH and replace with base+N.gHASH
    string(REGEX REPLACE "^(.+)-([0-9]+)-(g[0-9a-f]+)$" "\\1+\\2.\\3" BOOMERANG_FULL_VERSION "${VERSION_STRING}")
    message(STATUS "Version from git: ${BOOMERANG_FULL_VERSION}")
else()
    set(BOOMERANG_FULL_VERSION "${PROJECT_VERSION}-dev")
    message(STATUS "No git info found, using fallback: ${BOOMERANG_FULL_VERSION}")
endif()

# ==============================================================================
# Build Configuration
# ==============================================================================

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Path variables
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(CMAKE_DIR "${CMAKE_SOURCE_DIR}/cmake")

# macOS: Universal binary for Intel + Apple Silicon
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for macOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
endif()

# ==============================================================================
# Git Version Tracking
# ==============================================================================
# Generates git_version.h at BUILD time (not configure time).
# Only PluginEditor.cpp recompiles when the hash changes.

file(MAKE_DIRECTORY "${GENERATED_DIR}")

add_custom_target(UpdateGitVersion
    COMMAND ${CMAKE_COMMAND}
        -DGIT_WORKING_DIR="${CMAKE_SOURCE_DIR}"
        -DINPUT_FILE="${CMAKE_DIR}/git_version.h.in"
        -DOUTPUT_FILE="${GENERATED_DIR}/git_version.h"
        -P "${CMAKE_DIR}/UpdateGitVersion.cmake"
    BYPRODUCTS "${GENERATED_DIR}/git_version.h"
    COMMENT "Checking git version..."
)

# Initial git_version.h at configure time (for first build)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()
configure_file("${CMAKE_DIR}/git_version.h.in" "${GENERATED_DIR}/git_version.h" @ONLY)

# ==============================================================================
# JUCE Plugin Definition
# ==============================================================================

add_subdirectory(JUCE)

juce_add_plugin(Boomerang
    COMPANY_NAME "MC Music Workshop"
    BUNDLE_ID com.MCMusicWorkshop.Turrama
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Turrama needs microphone access to record loops."
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # PLUGIN_MANUFACTURER_CODE + PLUGIN_CODE identify the plugin to hosts
    # (VST3 class ID / AU subtype). Kept from the Boomerang+ era on purpose:
    # changing them would orphan the plugin in existing DAW sessions.
    PLUGIN_MANUFACTURER_CODE MCMW
    PLUGIN_CODE BOOM
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "Turrama"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# ==============================================================================
# Source Files & Dependencies
# ==============================================================================

target_sources(Boomerang
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/LooperEngine.cpp
)

juce_add_binary_data(BoomerangBinaryData
    SOURCES
        img/turrama.jpg
)

target_include_directories(Boomerang PRIVATE "${GENERATED_DIR}")
add_dependencies(Boomerang UpdateGitVersion)

target_compile_definitions(Boomerang
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_SILENCE_DUPLICATE_AUDIO_PERMISSION_REQUESTS=1
        BOOMERANG_VERSION="${BOOMERANG_FULL_VERSION}"
)

target_link_libraries(Boomerang
    PRIVATE
        BoomerangBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ==============================================================================
# Optional: ThreadSanitizer for Race Detection
# ==============================================================================

option(ENABLE_TSAN "Enable ThreadSanitizer for race detection" OFF)

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
    
    # Find TSan runtime library (macOS)
    if(APPLE)
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libclang_rt.tsan_osx_dynamic.dylib
            OUTPUT_VARIABLE TSAN_LIBRARY
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(EXISTS "${TSAN_LIBRARY}")
            message(STATUS "Found TSan library: ${TSAN_LIBRARY}")
            link_libraries(${TSAN_LIBRARY})
        endif()
    endif()
endif()

# ==============================================================================
# Validation Targets
# ==============================================================================

# Strict warnings build
add_custom_target(check-warnings
    COMMAND ${CMAKE_COMMAND} -E echo "Building with warnings as errors..."
    COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}/warnings-build
            -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DCMAKE_BUILD_TYPE=Debug
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/warnings-build -j4
    COMMENT "Building with -Werror to catch all warnings"
)

# pluginval validation (if installed)
find_program(PLUGINVAL pluginval PATHS 
    /Applications/pluginval.app/Contents/MacOS
    ~/Applications/pluginval.app/Contents/MacOS
    /usr/local/bin
)

if(PLUGINVAL)
    message(STATUS "Found pluginval: ${PLUGINVAL}")
    add_custom_target(validate
        COMMAND ${PLUGINVAL} --validate-in-process --verbose
                ${CMAKE_BINARY_DIR}/Boomerang_artefacts/VST3/Turrama.vst3
        DEPENDS Boomerang
        COMMENT "Validating plugin with pluginval"
    )
else()
    message(STATUS "pluginval not found - 'make validate' target unavailable")
    message(STATUS "Download: https://github.com/Tracktion/pluginval/releases")
endif()

