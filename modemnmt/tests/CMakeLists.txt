cmake_minimum_required(VERSION 3.16)
project(modemnmt_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(SDRANGEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(MODEMNMT_LIB_SOURCES
    ../biquad.cpp
    ../emphasis.cpp
    ../ffsk.cpp
    ../hagelbarger.cpp
    ../nmtbasestation.cpp
    ../nmtdms.cpp
    ../nmtcellularbridge.cpp
    ../nmtchannel.cpp
    ../nmtframe.cpp
    ../nmtmwi.cpp
    ../nmtmodem.cpp
    ../nmtnumbers.cpp
    ../nmtphonebook.cpp
    ../nmtphoneinfo.cpp
    ../nmtplan.cpp
    ../nmtsms.cpp
    ../nmtsystem.cpp
    ../nmtvoice.cpp
    ../../modemcellular/callrouter.cpp
    ../../modemcellular/carrierqueue.cpp
    ../../modemcellular/carrierradio.cpp
    ../../modemcellular/carrierwire.cpp
    ../../modemcellular/cellularsystem.cpp
    ../../modemcellular/clock.cpp
    ../../modemcellular/compandor.cpp
    ../../modemcellular/echoendpoint.cpp
    ../../modemcellular/firfilter.cpp
    ../../modemcellular/musicendpoint.cpp
    ../../modemcellular/timer.cpp
    ../../modemcellular/tonegenerator.cpp)
find_package(Threads REQUIRED)
find_package(Qt5 COMPONENTS Core REQUIRED)

# SDRangel's radio primitives the modules use, built from sdrbase as they are (needing only the Qt
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

# Sanitizer: address (address and undefined behaviour) or off
set(MODEMNMT_TEST_SANITIZER "off" CACHE STRING "off or address")

function(modemnmt_configure target)
    target_include_directories(${target} PRIVATE .. ${SDRANGEL_ROOT}/exports ${SDRANGEL_ROOT}/modemcellular)
    target_compile_definitions(${target} PRIVATE sdrangel_STATIC)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Werror)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND MODEMNMT_TEST_SANITIZER STREQUAL "address")
        # a finding of the undefined behaviour sanitizer has to fail the test
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
endfunction()

# the code under test, built once
add_library(modemnmt_under_test STATIC ${MODEMNMT_LIB_SOURCES})
modemnmt_configure(modemnmt_under_test)
target_link_libraries(modemnmt_under_test PUBLIC sdrbase_dsp Threads::Threads)

function(modemnmt_add_test name source)
    add_executable(${name} ${source})
    modemnmt_configure(${name})
    target_link_libraries(${name} PRIVATE modemnmt_under_test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

modemnmt_add_test(modemnmt_hagelbarger_test test_hagelbarger.cpp)
modemnmt_add_test(modemnmt_numbers_test test_numbers.cpp)
modemnmt_add_test(modemnmt_frame_test test_frames.cpp)
modemnmt_add_test(modemnmt_modem_test test_modem.cpp)
modemnmt_add_test(modemnmt_channel_test test_channel.cpp)
modemnmt_add_test(modemnmt_roaming_test test_roaming.cpp)
modemnmt_add_test(modemnmt_mocall_test test_mocall.cpp)
modemnmt_add_test(modemnmt_voice_test test_voice.cpp)
modemnmt_add_test(modemnmt_bridge_test test_bridge.cpp)
modemnmt_add_test(modemnmt_mtcall_test test_mtcall.cpp)
modemnmt_add_test(modemnmt_scip_test test_scip.cpp)
modemnmt_add_test(modemnmt_incall_test test_incall.cpp)
modemnmt_add_test(modemnmt_mwi_test test_mwi.cpp)
modemnmt_add_test(modemnmt_dms_test test_dms.cpp)
modemnmt_add_test(modemnmt_sms_test test_sms.cpp)
modemnmt_add_test(modemnmt_smscall_test test_smscall.cpp)
modemnmt_add_test(modemnmt_plan_test test_plan.cpp)
modemnmt_add_test(modemnmt_basestation_test test_basestation.cpp)
modemnmt_add_test(modemnmt_phoneinfo_test test_phoneinfo.cpp)
modemnmt_add_test(modemnmt_phonebook_test test_phonebook.cpp)
