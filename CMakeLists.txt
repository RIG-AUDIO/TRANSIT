# TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.22)
project(TRANSIT VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Extracted URL archives should carry extraction timestamps (CMP0135).
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# ---------------------------------------------------------------------------
# JUCE resolution.
#
# Public path (no local files): FetchContent with a pinned tag, so any
# contributor or CI runner can build from a clean clone.
# Local override: -DTRANSIT_JUCE_ROOT=<path> or the TRANSIT_JUCE_ROOT
# environment variable, for developers keeping a private JUCE checkout.
# ---------------------------------------------------------------------------
if(NOT DEFINED TRANSIT_JUCE_ROOT AND DEFINED ENV{TRANSIT_JUCE_ROOT})
  set(TRANSIT_JUCE_ROOT "$ENV{TRANSIT_JUCE_ROOT}")
endif()

if(DEFINED TRANSIT_JUCE_ROOT)
  if(NOT EXISTS "${TRANSIT_JUCE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
      "TRANSIT_JUCE_ROOT points at '${TRANSIT_JUCE_ROOT}', which is not a "
      "JUCE checkout. Unset it to fall back to the pinned FetchContent copy.")
  endif()
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    add_subdirectory(${TRANSIT_JUCE_ROOT} JUCE EXCLUDE_FROM_ALL SYSTEM)
  else()
    add_subdirectory(${TRANSIT_JUCE_ROOT} JUCE EXCLUDE_FROM_ALL)
  endif()
else()
  include(FetchContent)
  FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.1
    GIT_SHALLOW ON
  )
  FetchContent_MakeAvailable(juce)
endif()

# JUCE headers are third-party: consume them as system includes so strict
# first-party diagnostics never fire inside upstream header content. (Covers
# the FetchContent path and CMake < 3.25 where add_subdirectory has no
# SYSTEM keyword.)
foreach(_jcem juce_core juce_events juce_data_structures juce_graphics
              juce_gui_basics juce_gui_extra juce_audio_basics
              juce_audio_formats juce_audio_devices juce_audio_utils
              juce_audio_processors juce_dsp)
  if(TARGET ${_jcem})
    get_target_property(_jcem_includes ${_jcem} INTERFACE_INCLUDE_DIRECTORIES)
    if(_jcem_includes)
      set_property(TARGET ${_jcem} PROPERTY
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_jcem_includes}")
    endif()
  endif()
endforeach()

include(FetchContent)
FetchContent_Declare(
  miniaudio
  URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.21.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(miniaudio)

# MIT-licensed time-stretch backend (+ its MIT STFT dependency, fetched by
# its own CMakeLists).
FetchContent_Declare(
  signalsmith-stretch
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
  GIT_TAG 1.4.0
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(signalsmith-stretch)
# Treat the stretch headers as system headers: their diagnostics are
# upstream's, not ours, and must not fail our -Werror builds.
set_property(TARGET signalsmith-stretch PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
  $<TARGET_PROPERTY:signalsmith-stretch,INTERFACE_INCLUDE_DIRECTORIES>)

# Public-domain SQLite amalgamation: no CMakeLists inside, so build the
# single translation unit ourselves.
FetchContent_Declare(
  sqlite3
  URL https://www.sqlite.org/2025/sqlite-amalgamation-3510100.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)
if(EXISTS "${sqlite3_SOURCE_DIR}/sqlite3.c")
  set(SQLITE3_DIR "${sqlite3_SOURCE_DIR}")
else()
  file(GLOB SQLITE3_C "${sqlite3_SOURCE_DIR}/*/sqlite3.c")
  get_filename_component(SQLITE3_DIR "${SQLITE3_C}" DIRECTORY)
endif()
add_library(sqlite3 STATIC "${SQLITE3_DIR}/sqlite3.c")
target_include_directories(sqlite3 SYSTEM PUBLIC "${SQLITE3_DIR}")
if(MSVC)
  target_compile_options(sqlite3 PRIVATE /W0)
else()
  target_compile_options(sqlite3 PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# Strict first-party diagnostics.
#
# The intended gate: zero warnings from TRANSIT's own sources and tests on
# GCC and Clang. Third-party code (JUCE modules, miniaudio, signalsmith,
# SQLite) is kept behind system includes or separate targets so these flags
# never gate upstream sources.
# ---------------------------------------------------------------------------
set(TRANSIT_FIRSTPARTY_SOURCES
  Source/Main.cpp
  Source/MainComponent.cpp
  Source/SettingsDialog.cpp
  Source/TopBar.cpp
  Source/AudioEngine.cpp
  Source/Deck.cpp
  Source/Decode.cpp
  Source/Mixer.cpp
  Source/Library.cpp
  Source/Recorder.cpp
  Source/DJLook.cpp
  Source/FxRack.cpp
  Tools/ListAudioTypes.cpp
  Tools/UiShot.cpp
  Tools/FxProbe.cpp)

set(TRANSIT_WARNING_FLAGS)
if(MSVC)
  # Target-wide /WX for every first-party target (see the foreach below).
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(TRANSIT_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wformat=2
    -Wundef
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wimplicit-fallthrough
    -Werror
    # Supplementary checks kept from the earlier baseline; each targets a
    # concrete defect class or a known compiler false positive on our code.
    -Wstrict-aliasing
    -Wuninitialized
    -Wunused-parameter
    -Wsign-compare
    -Wunreachable-code
    -Wcast-align
    -Wswitch-enum
    -Wredundant-decls
    -Wfloat-equal
    -Wmissing-field-initializers
    -Wreorder
    -Wzero-as-null-pointer-constant
    -Wno-multichar
    -Wno-strict-overflow
    -Wno-ignored-qualifiers)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC-only diagnostics (Clang rejects the flag as unknown under -Werror).
    list(APPEND TRANSIT_WARNING_FLAGS -Wno-maybe-uninitialized)
  endif()
  set_source_files_properties(${TRANSIT_FIRSTPARTY_SOURCES} PROPERTIES
    COMPILE_OPTIONS "${TRANSIT_WARNING_FLAGS}")
endif()

# ---------------------------------------------------------------------------
# Application target
# ---------------------------------------------------------------------------
juce_add_gui_app(TRANSIT
  PRODUCT_NAME "TRANSIT"
  VERSION 0.2.0
  COMPANY_NAME "RIG-AUDIO"
)

# Embedded UI typefaces (OFL, see Source/Assets/OFL.txt).
juce_add_binary_data(TRANSITAssets
  NAMESPACE TransitFonts
  SOURCES
    Source/Assets/AdriaticaNext-Medium.ttf
    Source/Assets/AdriaticaNext-CondensedBold.ttf
)

target_sources(TRANSIT PRIVATE
  Source/Main.cpp
  Source/MainComponent.cpp
  Source/SettingsDialog.cpp
  Source/TopBar.cpp
  Source/AudioEngine.cpp
  Source/Deck.cpp
  Source/Decode.cpp
  Source/Mixer.cpp
  Source/Library.cpp
  Source/Recorder.cpp
  Source/DJLook.cpp
  Source/FxRack.cpp
)

target_include_directories(TRANSIT PRIVATE
  Source
)
target_include_directories(TRANSIT SYSTEM PRIVATE
  ${miniaudio_SOURCE_DIR}
)

target_link_libraries(TRANSIT PRIVATE
  juce::juce_gui_extra
  juce::juce_audio_utils
  juce::juce_audio_formats
  juce::juce_audio_processors
  juce::juce_dsp
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
  signalsmith-stretch
  sqlite3
  TRANSITAssets
)

target_compile_definitions(TRANSIT PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_APPLICATION_NAME_STRING="TRANSIT"
  JUCE_APPLICATION_VERSION_STRING="0.2.0"
)

if(WIN32)
  target_link_libraries(TRANSIT PRIVATE mfplat mfreadwrite mfuuid)
  # ASIO device support. JUCE modules compile as INTERFACE sources inside the
  # app target, so a PRIVATE define here reaches juce_audio_devices too.
  # NOTE: shipping ASIO support requires Steinberg's signed SDK agreement.
  target_compile_definitions(TRANSIT PRIVATE JUCE_ASIO=1)
endif()
# VST3 hosting (FX rack). Same INTERFACE-source rule as JUCE_ASIO above.
target_compile_definitions(TRANSIT PRIVATE JUCE_PLUGINHOST_VST3=1)

# ---------------------------------------------------------------------------
# Headless tests (plain C++ where possible; no JUCE, no hardware required)
# ---------------------------------------------------------------------------
include(CTest)
add_executable(NullRender Tests/NullRender.cpp Source/Deck.cpp)
target_include_directories(NullRender PRIVATE Source)
target_link_libraries(NullRender PRIVATE signalsmith-stretch)
add_test(NAME null_render COMMAND NullRender)

add_executable(DecodeTest Tests/DecodeTest.cpp Source/Decode.cpp)
target_include_directories(DecodeTest PRIVATE Source)
target_include_directories(DecodeTest SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
if(WIN32)
  target_link_libraries(DecodeTest PRIVATE mfplat mfreadwrite mfuuid)
endif()
add_test(NAME decode_test COMMAND DecodeTest)

add_executable(MixerTest Tests/MixerTest.cpp Source/Deck.cpp Source/Mixer.cpp)
target_include_directories(MixerTest PRIVATE Source)
target_link_libraries(MixerTest PRIVATE signalsmith-stretch)
add_test(NAME mixer_test COMMAND MixerTest)

add_executable(KeylockTest Tests/KeylockTest.cpp Source/Deck.cpp)
target_include_directories(KeylockTest PRIVATE Source)
target_link_libraries(KeylockTest PRIVATE signalsmith-stretch)
add_test(NAME keylock_test COMMAND KeylockTest)

add_executable(WaveformTest Tests/WaveformTest.cpp)
target_include_directories(WaveformTest PRIVATE Source)
add_test(NAME waveform_test COMMAND WaveformTest)

add_executable(LibraryTest Tests/LibraryTest.cpp Source/Library.cpp)
target_include_directories(LibraryTest PRIVATE Source)
target_link_libraries(LibraryTest PRIVATE sqlite3)
add_test(NAME library_test COMMAND LibraryTest)

add_executable(MidiMapTest Tests/MidiMapTest.cpp)
target_include_directories(MidiMapTest PRIVATE Source)
add_test(NAME midimap_test COMMAND MidiMapTest)

add_executable(RecorderTest Tests/RecorderTest.cpp Source/Recorder.cpp)
target_include_directories(RecorderTest PRIVATE Source)
add_test(NAME recorder_test COMMAND RecorderTest)

add_executable(AnalyzeTest Tests/AnalyzeTest.cpp)
target_include_directories(AnalyzeTest PRIVATE Source)
add_test(NAME analyze_test COMMAND AnalyzeTest)

add_executable(SyncTest Tests/SyncTest.cpp Source/Deck.cpp)
target_include_directories(SyncTest PRIVATE Source)
target_link_libraries(SyncTest PRIVATE signalsmith-stretch)
add_test(NAME sync_test COMMAND SyncTest)

# Manual load probe, not a gate (timing varies by machine).
add_executable(DspBench Tests/DspBench.cpp Source/Deck.cpp Source/Mixer.cpp)
target_include_directories(DspBench PRIVATE Source)
target_link_libraries(DspBench PRIVATE signalsmith-stretch)

# ---------------------------------------------------------------------------
# Dev tools (manual probes, not gates)
# ---------------------------------------------------------------------------

# Dev probe: lists visible audio device types/outputs. Run manually.
juce_add_console_app(ListAudioTypes)
target_sources(ListAudioTypes PRIVATE Tools/ListAudioTypes.cpp)
target_link_libraries(ListAudioTypes PRIVATE
  juce::juce_audio_devices
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)
target_compile_definitions(ListAudioTypes PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0)
if(WIN32)
  target_compile_definitions(ListAudioTypes PRIVATE JUCE_ASIO=1)
endif()

# Dev tool: offscreen UI snapshot for layout review (not a gate).
juce_add_console_app(UiShot)
target_sources(UiShot PRIVATE
  Tools/UiShot.cpp
  Source/MainComponent.cpp
  Source/SettingsDialog.cpp
  Source/TopBar.cpp
  Source/AudioEngine.cpp
  Source/Deck.cpp
  Source/Decode.cpp
  Source/Mixer.cpp
  Source/Library.cpp
  Source/Recorder.cpp
  Source/DJLook.cpp
  Source/FxRack.cpp)
target_include_directories(UiShot PRIVATE Source)
target_include_directories(UiShot SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(UiShot PRIVATE
  juce::juce_gui_extra
  juce::juce_audio_utils
  juce::juce_audio_formats
  juce::juce_audio_processors
  juce::juce_dsp
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
  signalsmith-stretch
  sqlite3
  TRANSITAssets)
if(WIN32)
  target_link_libraries(UiShot PRIVATE mfplat mfreadwrite mfuuid)
  target_compile_definitions(UiShot PRIVATE JUCE_ASIO=1)
endif()
target_compile_definitions(UiShot PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0)

# Dev probe: end-to-end VST3 load/process/unload on this machine.
juce_add_console_app(FxProbe)
target_sources(FxProbe PRIVATE
  Tools/FxProbe.cpp
  Source/FxRack.cpp)
target_include_directories(FxProbe PRIVATE Source)
target_link_libraries(FxProbe PRIVATE
  juce::juce_gui_extra
  juce::juce_audio_processors
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
  signalsmith-stretch)
target_compile_definitions(FxProbe PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0)

# ---------------------------------------------------------------------------
# Zero-warning enforcement.
#
# Lean test/tool targets contain only first-party sources (third-party code
# arrives through system includes or separate targets), so -Werror is safe
# target-wide there. JUCE-linked app targets also compile JUCE's own module
# sources internally; on those targets -Werror is attached per first-party
# source file (above) so upstream sources are never gated by it.
# ---------------------------------------------------------------------------
foreach(t NullRender DecodeTest MixerTest KeylockTest WaveformTest LibraryTest
          MidiMapTest RecorderTest AnalyzeTest SyncTest DspBench)
  if(MSVC)
    target_compile_options(${t} PRIVATE /WX)
  else()
    target_compile_options(${t} PRIVATE ${TRANSIT_WARNING_FLAGS})
  endif()
endforeach()

foreach(t TRANSIT ListAudioTypes UiShot FxProbe)
  if(MSVC)
    target_compile_options(${t} PRIVATE /WX)
  endif()
endforeach()
