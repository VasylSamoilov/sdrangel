project(modemnmt)

set(modemnmt_SOURCES
    biquad.cpp
    ffsk.cpp
    hagelbarger.cpp
    nmtcellularbridge.cpp
    nmtchannel.cpp
    nmtframe.cpp
    nmtmwi.cpp
    nmtmodem.cpp
    nmtnumbers.cpp
    nmtsystem.cpp
    nmtvoice.cpp
)

set(modemnmt_HEADERS
    biquad.h
    ffsk.h
    hagelbarger.h
    nmtcellularbridge.h
    nmtchannel.h
    nmtframe.h
    nmtmwi.h
    nmtmodem.h
    nmtnumbers.h
    nmtsystem.h
    nmtvoice.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/exports
)

add_library(modemnmt
    ${modemnmt_SOURCES}
)

# the headers include the timer and clock of modemcellular
target_include_directories(modemnmt PUBLIC ${CMAKE_SOURCE_DIR}/modemcellular)

target_link_libraries(modemnmt modemcellular)

install(TARGETS modemnmt DESTINATION ${INSTALL_LIB_DIR})
