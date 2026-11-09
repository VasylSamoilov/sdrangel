cmake_minimum_required(VERSION 3.16)
project(modemcellular_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
get_filename_component(SDRANGEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(MODEMCELLULAR_LIB_SOURCES
    ../callrouter.cpp
    ../cellularsystem.cpp
    ../clock.cpp
    ../echoendpoint.cpp
    ../musicendpoint.cpp
    ../timer.cpp
    ../tonegenerator.cpp)

# Sanitizer: address (address and undefined behaviour), thread, or off
set(MODEMCELLULAR_TEST_SANITIZER "off" CACHE STRING "off, address or thread")

function(modemcellular_add_test name source)
    add_executable(${name} ${source} ${MODEMCELLULAR_LIB_SOURCES})
    target_include_directories(${name} PRIVATE .. ${SDRANGEL_ROOT}/exports)
    target_compile_definitions(${name} PRIVATE sdrangel_STATIC)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(${name} PRIVATE Threads::Threads)
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
modemcellular_add_test(modemcellular_router_test test_router.cpp)
modemcellular_add_test(modemcellular_system_test test_system.cpp)
