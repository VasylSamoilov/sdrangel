project(modemcellular)

set(modemcellular_SOURCES
    callrouter.cpp
    cellularsystem.cpp
    clock.cpp
    dtmfgenerator.cpp
    echoendpoint.cpp
    musicendpoint.cpp
    timer.cpp
    tonegenerator.cpp
)

set(modemcellular_HEADERS
    audioendpoint.h
    callrouter.h
    calltypes.h
    cellularsystem.h
    clock.h
    dtmfgenerator.h
    echoendpoint.h
    mobileleg.h
    musicendpoint.h
    timer.h
    tonegenerator.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/exports
)

add_library(modemcellular
    ${modemcellular_SOURCES}
)

find_package(Threads REQUIRED)
target_link_libraries(modemcellular Threads::Threads)

install(TARGETS modemcellular DESTINATION ${INSTALL_LIB_DIR})
