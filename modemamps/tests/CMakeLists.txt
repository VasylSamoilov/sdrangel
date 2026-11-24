cmake_minimum_required(VERSION 3.16)
project(modemamps_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(SDRANGEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(MODEMAMPS_LIB_SOURCES
    ../ampsbch.cpp
    ../ampsstream.cpp
    ../ampsword.cpp
    ../ampsmessages.cpp
    ../ampsfsk.cpp
    ../ampsframe.cpp
    ../ampssysinfo.cpp
    ../ampsfocc.cpp
    ../ampsfvc.cpp
    ../ampsvoice.cpp
    ../ampsrecc.cpp
    ../ampsplan.cpp
    ../ampsspeech.cpp
    ../../modemcellular/biquad.cpp
    ../../modemcellular/compandor.cpp
    ../../modemcellular/emphasis.cpp
    ../../modemcellular/txaudiosource.cpp)
find_package(Qt5 COMPONENTS Core REQUIRED)

# SDRangel's radio primitives the modules use, built from sdrbase as they are (needing only the Qt
# headers), without the warnings of our own code made errors
set(SDRBASE_DSP_SOURCES
    ${SDRANGEL_ROOT}/sdrbase/dsp/fmpreemphasis.cpp
    ${SDRANGEL_ROOT}/sdrbase/dsp/goertzel.cpp
    ${SDRANGEL_ROOT}/sdrbase/dsp/interpolator.cpp
    ${SDRANGEL_ROOT}/sdrbase/dsp/nco.cpp)
add_library(sdrbase_dsp STATIC ${SDRBASE_DSP_SOURCES})
target_include_directories(sdrbase_dsp PUBLIC ${SDRANGEL_ROOT}/sdrbase ${SDRANGEL_ROOT}/exports)
target_compile_definitions(sdrbase_dsp PUBLIC sdrangel_STATIC)
target_link_libraries(sdrbase_dsp PUBLIC Qt5::Core)
# the SIMD paths of the headers (Interpolator) as SDRangel's build has them on x86-64; off for a
# suite of the plain C++ paths (ARM: the Pi, Android)
option(MODEMAMPS_TEST_SSE2 "Build the SSE2 paths of sdrbase as SDRangel does on x86-64" ON)
if(MODEMAMPS_TEST_SSE2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_definitions(sdrbase_dsp PUBLIC USE_SSE2)
endif()

# Sanitizer: address (address and undefined behaviour) or off
set(MODEMAMPS_TEST_SANITIZER "off" CACHE STRING "off or address")

function(modemamps_configure target)
    target_include_directories(${target} PRIVATE .. ${SDRANGEL_ROOT}/exports ${SDRANGEL_ROOT}/modemcellular)
    target_compile_definitions(${target} PRIVATE sdrangel_STATIC)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Werror)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND MODEMAMPS_TEST_SANITIZER STREQUAL "address")
        # a finding of the undefined behaviour sanitizer has to fail the test
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
endfunction()

# the code under test, built once
add_library(modemamps_under_test STATIC ${MODEMAMPS_LIB_SOURCES})
modemamps_configure(modemamps_under_test)
target_link_libraries(modemamps_under_test PUBLIC sdrbase_dsp)

function(modemamps_add_test name source)
    add_executable(${name} ${source})
    modemamps_configure(${name})
    target_link_libraries(${name} PRIVATE modemamps_under_test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

modemamps_add_test(modemamps_bch_test test_bch.cpp)
modemamps_add_test(modemamps_stream_test test_stream.cpp)
modemamps_add_test(modemamps_word_test test_word.cpp)
modemamps_add_test(modemamps_messages_test test_messages.cpp)
modemamps_add_test(modemamps_fsk_test test_fsk.cpp)
modemamps_add_test(modemamps_frame_test test_frame.cpp)
modemamps_add_test(modemamps_sysinfo_test test_sysinfo.cpp)
modemamps_add_test(modemamps_focc_test test_focc.cpp)
modemamps_add_test(modemamps_fvc_test test_fvc.cpp)
modemamps_add_test(modemamps_voice_test test_voice.cpp)
modemamps_add_test(modemamps_recc_test test_recc.cpp)
modemamps_add_test(modemamps_plan_test test_plan.cpp)
modemamps_add_test(modemamps_speech_test test_speech.cpp)
