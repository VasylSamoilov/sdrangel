cmake_minimum_required(VERSION 3.16)
project(modemnmt_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(SDRANGEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(MODEMNMT_LIB_SOURCES
    ../biquad.cpp
    ../ffsk.cpp
    ../hagelbarger.cpp
    ../nmtchannel.cpp
    ../nmtframe.cpp
    ../nmtmodem.cpp
    ../nmtnumbers.cpp)

# Sanitizer: address (address and undefined behaviour) or off
set(MODEMNMT_TEST_SANITIZER "off" CACHE STRING "off or address")

function(modemnmt_add_test name source)
    add_executable(${name} ${source} ${MODEMNMT_LIB_SOURCES})
    target_include_directories(${name} PRIVATE .. ${SDRANGEL_ROOT}/exports ${SDRANGEL_ROOT}/modemcellular)
    target_compile_definitions(${name} PRIVATE sdrangel_STATIC)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND MODEMNMT_TEST_SANITIZER STREQUAL "address")
        target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

modemnmt_add_test(modemnmt_hagelbarger_test test_hagelbarger.cpp)
modemnmt_add_test(modemnmt_numbers_test test_numbers.cpp)
modemnmt_add_test(modemnmt_frame_test test_frames.cpp)
modemnmt_add_test(modemnmt_modem_test test_modem.cpp)
modemnmt_add_test(modemnmt_channel_test test_channel.cpp)
