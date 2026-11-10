project(modemcellular)

set(modemcellular_SOURCES
    callrouter.cpp
    carrierqueue.cpp
    carrierradio.cpp
    cellularsystem.cpp
    clock.cpp
    compandor.cpp
    dtmfgenerator.cpp
    echoendpoint.cpp
    firfilter.cpp
    musicendpoint.cpp
    timer.cpp
    tonegenerator.cpp
)

set(modemcellular_HEADERS
    audioendpoint.h
    callrouter.h
    calltypes.h
    carrierbatch.h
    carrierqueue.h
    carrierradio.h
    cellularsystem.h
    clock.h
    compandor.h
    dtmfgenerator.h
    echoendpoint.h
    firfilter.h
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
