project(modemradiopaging)

set(modemradiopaging_SOURCES
    radiopagingbch.cpp
    radiopagingfskdemod.cpp
    radiopagingpocsag.cpp
    radiopagingpocsagdecoder.cpp
    radiopaginggsc.cpp
    radiopaginggscdecoder.cpp
    radiopagingflex.cpp
    radiopagingflexscheduler.cpp
)

set(modemradiopaging_HEADERS
    radiopagingbch.h
    radiopagingfskdemod.h
    radiopagingpocsag.h
    radiopagingpocsagdecoder.h
    radiopaginggsc.h
    radiopaginggscdecoder.h
    radiopagingflex.h
    radiopagingflexscheduler.h
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
#   cmake --build <build dir> --target radiopagingtest radiopaginggsctest radiopagingflextest
foreach(radiopaging_test radiopagingtest radiopaginggsctest radiopagingflextest)
    add_executable(${radiopaging_test} EXCLUDE_FROM_ALL test/${radiopaging_test}.cpp)
    target_link_libraries(${radiopaging_test} modemradiopaging)
    target_include_directories(${radiopaging_test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
endforeach()
