project(modemradiopaging)

set(modemradiopaging_SOURCES
    radiopagingbch.cpp
    radiopagingfskdemod.cpp
    radiopagingfskmod.cpp
    radiopagingpocsag.cpp
    radiopagingpocsagdecoder.cpp
    radiopagingpocsagreceiver.cpp
    radiopaginggsc.cpp
    radiopaginggscdecoder.cpp
    radiopaginggscreceiver.cpp
    radiopagingflex.cpp
    radiopagingflexscheduler.cpp
    radiopagingflexdecoder.cpp
    radiopagingflexdecodermsg.cpp
    radiopagingflexreceiver.cpp
    radiopagingtxstream.cpp
)

set(modemradiopaging_HEADERS
    radiopagingbch.h
    radiopagingberwindow.h
    radiopagingdedup.h
    radiopagingfskdemod.h
    radiopagingfskmod.h
    radiopagingpocsag.h
    radiopagingpocsagdecoder.h
    radiopagingpocsagreceiver.h
    radiopaginggsc.h
    radiopaginggscdecoder.h
    radiopaginggscreceiver.h
    radiopagingflex.h
    radiopagingflexscheduler.h
    radiopagingflexdecoder.h
    radiopagingflexreceiver.h
    radiopagingtxstream.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/exports
    ${CMAKE_SOURCE_DIR}/sdrbase
)

add_library(modemradiopaging
    ${modemradiopaging_SOURCES}
)

target_link_libraries(modemradiopaging
    Qt::Core
    sdrbase
)

install(TARGETS modemradiopaging DESTINATION ${INSTALL_LIB_DIR})

# Self tests of the codecs and the demodulator, not built by default:
#   cmake --build <build dir> --target radiopagingtest radiopaginggsctest radiopagingflextest radiopagingflexdecodertest radiopagingflexreceivertest radiopagingfskmodtest radiopagingtxstreamtest
foreach(radiopaging_test radiopagingtest radiopaginggsctest radiopagingflextest radiopagingflexdecodertest radiopagingflexreceivertest radiopagingfskmodtest radiopagingtxstreamtest)
    add_executable(${radiopaging_test} EXCLUDE_FROM_ALL test/${radiopaging_test}.cpp)
    target_link_libraries(${radiopaging_test} modemradiopaging)
    target_include_directories(${radiopaging_test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
endforeach()
