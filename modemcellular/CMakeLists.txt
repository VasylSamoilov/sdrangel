project(modemcellular)

set(modemcellular_SOURCES
    callrouter.cpp
    carrierqueue.cpp
    carrierradio.cpp
    carrierwire.cpp
    cellularsystem.cpp
    clock.cpp
    compandor.cpp
    echoendpoint.cpp
    firfilter.cpp
    musicendpoint.cpp
    testcaller.cpp
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
    carrierwire.h
    cellularsystem.h
    clock.h
    compandor.h
    echoendpoint.h
    firfilter.h
    mobileleg.h
    musicendpoint.h
    testcaller.h
    timer.h
    tonegenerator.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/exports
    ${CMAKE_SOURCE_DIR}/sdrbase
)

add_library(modemcellular
    ${modemcellular_SOURCES}
)

# the radio primitives (filters, resampling, NCO, tone detection) are SDRangel's own
find_package(Threads REQUIRED)
target_link_libraries(modemcellular
    Qt::Core
    sdrbase
    Threads::Threads
)

install(TARGETS modemcellular DESTINATION ${INSTALL_LIB_DIR})
