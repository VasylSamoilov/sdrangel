project(modemnmt)

set(modemnmt_SOURCES
    biquad.cpp
    ffsk.cpp
    hagelbarger.cpp
    nmtchannel.cpp
    nmtframe.cpp
    nmtmodem.cpp
    nmtnumbers.cpp
)

set(modemnmt_HEADERS
    biquad.h
    ffsk.h
    hagelbarger.h
    nmtchannel.h
    nmtframe.h
    nmtmodem.h
    nmtnumbers.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/exports
    ${CMAKE_SOURCE_DIR}/modemcellular
)

add_library(modemnmt
    ${modemnmt_SOURCES}
)

install(TARGETS modemnmt DESTINATION ${INSTALL_LIB_DIR})
