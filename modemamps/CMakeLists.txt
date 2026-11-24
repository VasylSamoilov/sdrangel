project(modemamps)

set(modemamps_SOURCES
    ampsbch.cpp
    ampsstream.cpp
    ampsword.cpp
    ampsmessages.cpp
    ampsfsk.cpp
    ampsframe.cpp
    ampssysinfo.cpp
    ampsfocc.cpp
    ampsfvc.cpp
    ampsvoice.cpp
    ampsrecc.cpp
    ampsplan.cpp
)

set(modemamps_HEADERS
    ampsbch.h
    ampsstream.h
    ampsword.h
    ampsmessages.h
    ampsfsk.h
    ampsframe.h
    ampssysinfo.h
    ampsfocc.h
    ampsfvc.h
    ampsvoice.h
    ampsrecc.h
    ampsplan.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/exports
    ${CMAKE_SOURCE_DIR}/sdrbase
)

add_library(modemamps
    ${modemamps_SOURCES}
)

# the headers include the timer and clock of modemcellular
target_include_directories(modemamps PUBLIC ${CMAKE_SOURCE_DIR}/modemcellular)

target_link_libraries(modemamps
    Qt::Core
    sdrbase
    modemcellular
)

install(TARGETS modemamps DESTINATION ${INSTALL_LIB_DIR})
