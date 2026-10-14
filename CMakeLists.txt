cmake_minimum_required(VERSION 3.16)
project(RaneDriver VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_MACOSX_RPATH TRUE)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREMIDI_FRAMEWORK CoreMIDI REQUIRED)
endif()

# Protocol C sources
add_library(rane_proto STATIC
    src/protocol/rane_codec.c
    src/protocol/rane_packet.c
    src/protocol/sixty_four_audio.c
)
target_include_directories(rane_proto PUBLIC src/protocol)

# Transport & Engine C++ sources
add_library(rane_engine STATIC
    src/transport/IOUSBLibTransport.cpp
    src/engine/StreamEngine.cpp
    src/engine/MidiBridge.cpp
)
target_include_directories(rane_engine PUBLIC src/protocol src/transport src/engine)
target_link_libraries(rane_engine PUBLIC rane_proto ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${COREAUDIO_FRAMEWORK})

# 1. HAL AudioServerPlugIn Bundle
add_library(RaneHAL MODULE
    src/hal/RaneHALPlugin.cpp
)
set_target_properties(RaneHAL PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Info.plist"
    OUTPUT_NAME "RaneHAL"
)
target_link_libraries(RaneHAL PRIVATE rane_engine ${COREAUDIO_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${IOKIT_FRAMEWORK})

# 2. MIDI LaunchAgent Daemon
add_executable(RaneMIDIAgent
    src/midi-agent/main.cpp
)
target_link_libraries(RaneMIDIAgent PRIVATE ${COREMIDI_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${IOKIT_FRAMEWORK})

# 3. Selftest Tool & Stress Test Suite
add_executable(rane-selftest
    src/selftest/main.cpp
)
target_link_libraries(rane-selftest PRIVATE rane_engine ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})

add_executable(rane-stresstest
    src/stresstest/main.cpp
)
target_link_libraries(rane-stresstest PRIVATE ${COREAUDIO_FRAMEWORK} ${COREMIDI_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${IOKIT_FRAMEWORK})

# 4. Unit Tests
enable_testing()

# Offline MIDI envelope candidate; deliberately not linked into a live driver.
add_executable(test_sixty_four_midi tests/test_sixty_four_midi.cpp)
target_compile_options(test_sixty_four_midi PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_sixty_four_midi COMMAND test_sixty_four_midi)

add_executable(test_codec tests/test_codec.cpp)
target_link_libraries(test_codec PRIVATE rane_proto)
add_test(NAME test_codec COMMAND test_codec)

add_executable(test_packet tests/test_packet.cpp)
target_link_libraries(test_packet PRIVATE rane_proto)
add_test(NAME test_packet COMMAND test_packet)

add_executable(test_ring tests/test_ring.cpp)
target_include_directories(test_ring PRIVATE src/engine)
add_test(NAME test_ring COMMAND test_ring)

# Pure metadata test: no engine, USB access or driver loading.
add_executable(test_device_lookup tests/test_device_lookup.cpp)
add_test(NAME test_device_lookup COMMAND test_device_lookup)

# Standalone cached-descriptor reader; never links the hardware engine.
if(APPLE)
    add_executable(read_sixty_four_descriptors tools/debug/read_sixty_four_descriptors.c)
    target_link_libraries(read_sixty_four_descriptors PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

# Active diagnostic; requires explicit flag and never runs under CTest.
if(APPLE)
    add_executable(capture_sixty_four_controls tools/debug/capture_sixty_four_controls.c)
    target_link_libraries(capture_sixty_four_controls PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

if(APPLE)
    add_executable(test_capture_callback tests/test_capture_callback.c)
    target_link_libraries(test_capture_callback PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    add_test(NAME test_capture_callback COMMAND test_capture_callback)
endif()

# Active recording-only diagnostic, never run by CTest.
if(APPLE)
    add_executable(record_sixty_four_packets tools/debug/record_sixty_four_packets.c)
    target_link_libraries(record_sixty_four_packets PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

add_executable(test_sixty_four_audio tests/test_sixty_four_audio.cpp)
target_link_libraries(test_sixty_four_audio PRIVATE rane_proto)
add_test(NAME test_sixty_four_audio COMMAND test_sixty_four_audio)

# Pure scheduling logic: no USB API linkage or device access.
add_executable(test_recording_schedule tests/test_recording_schedule.cpp)
add_test(NAME test_recording_schedule COMMAND test_recording_schedule)

add_executable(test_recording_buffer tests/test_recording_buffer.cpp)
target_link_libraries(test_recording_buffer PRIVATE rane_proto)
add_test(NAME test_recording_buffer COMMAND test_recording_buffer)
if(APPLE)
    # Standalone opt-in recorder: not linked to the HAL or existing USB engine.
    add_executable(record_sixty_four_continuous tools/debug/record_sixty_four_continuous.cpp)
    target_link_libraries(record_sixty_four_continuous PRIVATE rane_proto ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    target_compile_options(record_sixty_four_continuous PRIVATE -Wall -Wextra -Werror)
endif()

if(APPLE)
    add_library(sixty_four_capture STATIC src/transport/SixtyFourCapture.cpp)
    target_link_libraries(sixty_four_capture PUBLIC rane_proto ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    add_library(RaneSixtyFourInput MODULE src/hal64/SixtyFourInput.cpp)
    set_target_properties(RaneSixtyFourInput PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "driver"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/hal64/Info.plist")
    target_link_libraries(RaneSixtyFourInput PRIVATE sixty_four_capture ${COREAUDIO_FRAMEWORK})
endif()

add_executable(test_capture_timeline tests/test_capture_timeline.cpp)
add_test(NAME test_capture_timeline COMMAND test_capture_timeline)
if(APPLE)
    target_compile_options(sixty_four_capture PRIVATE -Wall -Wextra -Werror)
    target_compile_options(RaneSixtyFourInput PRIVATE -Wall -Wextra -Werror)
    # Source-level HAL contract test with fake capture: no USB opens or driver loading.
    add_executable(test_sixty_four_hal tests/test_sixty_four_hal.cpp)
    target_link_libraries(test_sixty_four_hal PRIVATE rane_proto ${COREAUDIO_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    add_test(NAME test_sixty_four_hal COMMAND test_sixty_four_hal)
endif()

# Offline playback preparation only: deliberately not linked into either HAL.
add_library(sixty_four_playback_candidate STATIC src/playback/SixtyFourPlayback.cpp)
target_compile_options(sixty_four_playback_candidate PRIVATE -Wall -Wextra -Werror)
add_executable(test_sixty_four_playback tests/test_sixty_four_playback.cpp)
target_link_libraries(test_sixty_four_playback PRIVATE sixty_four_playback_candidate)
add_test(NAME test_sixty_four_playback COMMAND test_sixty_four_playback)
if(APPLE)
    add_executable(test_sixty_four_output_profile tests/test_sixty_four_output_profile.cpp)
    target_link_libraries(test_sixty_four_output_profile PRIVATE rane_proto ${COREAUDIO_FRAMEWORK})
    add_test(NAME test_sixty_four_output_profile COMMAND test_sixty_four_output_profile)
endif()
set_tests_properties(test_sixty_four_playback PROPERTIES TIMEOUT 30)

if(APPLE)
    # Active only with explicit command-line mode; never registered with CTest.
    add_executable(play_sixty_four_probe tools/debug/play_sixty_four_probe.cpp)
    target_link_libraries(play_sixty_four_probe PRIVATE sixty_four_playback_candidate ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    target_compile_options(play_sixty_four_probe PRIVATE -Wall -Wextra -Werror)
endif()

if(APPLE)
    # Experimental duplex transport; recording-only installed target stays separate.
    add_library(sixty_four_duplex STATIC src/transport/SixtyFourDuplex.cpp)
    target_link_libraries(sixty_four_duplex PUBLIC sixty_four_playback_candidate rane_proto ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    target_compile_options(sixty_four_duplex PRIVATE -Wall -Wextra -Werror)
endif()

if(APPLE)
    add_library(RaneSixtyFourDuplex MODULE src/hal64/SixtyFourDuplex.cpp)
    set_target_properties(RaneSixtyFourDuplex PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "driver"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/hal64/DuplexInfo.plist")
    target_link_libraries(RaneSixtyFourDuplex PRIVATE sixty_four_duplex ${COREAUDIO_FRAMEWORK})
    target_compile_options(RaneSixtyFourDuplex PRIVATE -Wall -Wextra -Werror)
endif()

if(APPLE)
    add_executable(probe_sixty_four_duplex tools/debug/probe_sixty_four_duplex.cpp)
    target_link_libraries(probe_sixty_four_duplex PRIVATE sixty_four_duplex)
    target_compile_options(probe_sixty_four_duplex PRIVATE -Wall -Wextra -Werror)
    add_executable(test_sixty_four_duplex_hal tests/test_sixty_four_duplex_hal.cpp)
    target_link_libraries(test_sixty_four_duplex_hal PRIVATE sixty_four_playback_candidate rane_proto ${COREAUDIO_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    add_test(NAME test_sixty_four_duplex_hal COMMAND test_sixty_four_duplex_hal)
endif()

if(APPLE)
    add_executable(sixty_four_diagnostics tools/debug/sixty_four_diagnostics.cpp)
    target_link_libraries(sixty_four_diagnostics PRIVATE ${COREAUDIO_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    target_compile_options(sixty_four_diagnostics PRIVATE -Wall -Wextra -Werror)
endif()

# Sixty-Four foreground MIDI bridge: no installer or automatic launch.
add_executable(test_sixty_four_midi_stream tests/test_sixty_four_midi_stream.cpp)
target_compile_options(test_sixty_four_midi_stream PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_sixty_four_midi_stream COMMAND test_sixty_four_midi_stream)
if(APPLE)
    add_executable(sixty_four_midi_bridge src/midi64/main.cpp)
    target_link_libraries(sixty_four_midi_bridge PRIVATE ${COREMIDI_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${IOKIT_FRAMEWORK})
    target_compile_options(sixty_four_midi_bridge PRIVATE -Wall -Wextra -Werror -Wno-deprecated-declarations)
endif()
if(APPLE)
    add_executable(test_sixty_four_midi_bridge tests/test_sixty_four_midi_bridge.cpp)
    target_link_libraries(test_sixty_four_midi_bridge PRIVATE ${COREMIDI_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${IOKIT_FRAMEWORK})
    target_compile_options(test_sixty_four_midi_bridge PRIVATE -Wall -Wextra -Werror -Wno-deprecated-declarations)
    add_test(NAME test_sixty_four_midi_bridge COMMAND test_sixty_four_midi_bridge)
endif()
if(APPLE)
    add_executable(monitor_sixty_four_midi tools/debug/monitor_sixty_four_midi.cpp)
    add_executable(inventory_sixty_four_midi tools/debug/inventory_sixty_four_midi.cpp)
    target_link_libraries(inventory_sixty_four_midi PRIVATE ${COREMIDI_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    target_compile_options(inventory_sixty_four_midi PRIVATE -Wall -Wextra -Werror -Wno-deprecated-declarations)
    target_link_libraries(monitor_sixty_four_midi PRIVATE ${COREMIDI_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    target_compile_options(monitor_sixty_four_midi PRIVATE -Wall -Wextra -Werror -Wno-deprecated-declarations)
endif()
