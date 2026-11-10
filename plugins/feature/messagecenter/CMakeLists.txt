project(messagecenter)

set(messagecenter_SOURCES
    messagecenterengine.cpp
)

set(messagecenter_HEADERS
    messagecenterengine.h
)

# Self test of the engine, not built by default:
#   cmake --build <build dir> --target messagecenterenginetest
if(NOT SERVER_MODE)
    add_executable(messagecenterenginetest EXCLUDE_FROM_ALL
        test/messagecenterenginetest.cpp
        messagecenterengine.cpp
    )
    target_include_directories(messagecenterenginetest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(messagecenterenginetest Qt::Core sdrbase)
endif()
