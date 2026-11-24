cmake_minimum_required(VERSION 3.16)
project(modemamps_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(SDRANGEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(MODEMAMPS_LIB_SOURCES
    ../ampsbch.cpp
    ../ampsstream.cpp)

# Sanitizer: address (address and undefined behaviour) or off
set(MODEMAMPS_TEST_SANITIZER "off" CACHE STRING "off or address")

function(modemamps_configure target)
    target_include_directories(${target} PRIVATE .. ${SDRANGEL_ROOT}/exports)
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

function(modemamps_add_test name source)
    add_executable(${name} ${source})
    modemamps_configure(${name})
    target_link_libraries(${name} PRIVATE modemamps_under_test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

modemamps_add_test(modemamps_bch_test test_bch.cpp)
modemamps_add_test(modemamps_stream_test test_stream.cpp)
