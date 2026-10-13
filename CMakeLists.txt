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
