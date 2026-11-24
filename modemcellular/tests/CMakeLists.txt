cmake_minimum_required(VERSION 3.16)
project(modemcellular_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
get_filename_component(SDRANGEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(MODEMCELLULAR_LIB_SOURCES
    ../biquad.cpp
    ../calllistener.cpp
    ../callrouter.cpp
    ../carrierqueue.cpp
    ../carrierradio.cpp
    ../carrierwire.cpp
    ../cellularsystem.cpp
    ../clock.cpp
    ../compandor.cpp
    ../consoleleg.cpp
    ../echoendpoint.cpp
    ../echosuppressor.cpp
    ../emphasis.cpp
    ../firfilter.cpp
    ../musicendpoint.cpp
    ../speechmeter.cpp
    ../testcaller.cpp
    ../timer.cpp
    ../tonegenerator.cpp)

find_package(Qt5 COMPONENTS Core REQUIRED)

# SDRangel's radio primitives the module uses, built from sdrbase as they are (needing only the Qt
# headers), without the warnings of our own code made errors
set(SDRBASE_DSP_SOURCES
    ${SDRANGEL_ROOT}/sdrbase/dsp/dv/dvdtmf.cpp
    ${SDRANGEL_ROOT}/sdrbase/dsp/firfilter.cpp
    ${SDRANGEL_ROOT}/sdrbase/dsp/goertzel.cpp
    ${SDRANGEL_ROOT}/sdrbase/dsp/interpolator.cpp
    ${SDRANGEL_ROOT}/sdrbase/dsp/nco.cpp)
add_library(sdrbase_dsp STATIC ${SDRBASE_DSP_SOURCES})
target_include_directories(sdrbase_dsp PUBLIC ${SDRANGEL_ROOT}/sdrbase ${SDRANGEL_ROOT}/exports)
target_compile_definitions(sdrbase_dsp PUBLIC sdrangel_STATIC)
target_link_libraries(sdrbase_dsp PUBLIC Qt5::Core)
# the SIMD paths of the headers (Interpolator) as SDRangel's build has them on x86-64; off for a
# suite of the plain C++ paths (ARM: the Pi, Android)
option(MODEMCELLULAR_TEST_SSE2 "Build the SSE2 paths of sdrbase as SDRangel does on x86-64" ON)
if(MODEMCELLULAR_TEST_SSE2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_definitions(sdrbase_dsp PUBLIC USE_SSE2)
endif()

# Sanitizer: address (address and undefined behaviour), thread, or off
set(MODEMCELLULAR_TEST_SANITIZER "off" CACHE STRING "off, address or thread")

function(modemcellular_add_test name source)
    add_executable(${name} ${source} ${MODEMCELLULAR_LIB_SOURCES})
    target_include_directories(${name} PRIVATE .. ${SDRANGEL_ROOT}/exports)
    target_compile_definitions(${name} PRIVATE sdrangel_STATIC)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(${name} PRIVATE sdrbase_dsp Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(MODEMCELLULAR_TEST_SANITIZER STREQUAL "address")
            target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${name} PRIVATE -fsanitize=address,undefined)
        elseif(MODEMCELLULAR_TEST_SANITIZER STREQUAL "thread")
            target_compile_options(${name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
            target_link_options(${name} PRIVATE -fsanitize=thread)
        endif()
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

modemcellular_add_test(modemcellular_timer_test test_timer.cpp)
modemcellular_add_test(modemcellular_endpoint_test test_endpoints.cpp)
modemcellular_add_test(modemcellular_tone_test test_tones.cpp)
modemcellular_add_test(modemcellular_compandor_test test_compandor.cpp)
modemcellular_add_test(modemcellular_biquad_test test_biquad.cpp)
modemcellular_add_test(modemcellular_echo_test test_echo.cpp)
modemcellular_add_test(modemcellular_router_test test_router.cpp)
modemcellular_add_test(modemcellular_system_test test_system.cpp)
modemcellular_add_test(modemcellular_carrierqueue_test test_carrierqueue.cpp)
modemcellular_add_test(modemcellular_carrierradio_test test_carrierradio.cpp)
modemcellular_add_test(modemcellular_carrierwire_test test_carrierwire.cpp)
modemcellular_add_test(modemcellular_testcaller_test test_testcaller.cpp)
modemcellular_add_test(modemcellular_speechmeter_test test_speechmeter.cpp)
modemcellular_add_test(modemcellular_calllistener_test test_calllistener.cpp)
modemcellular_add_test(modemcellular_console_test test_console.cpp)
