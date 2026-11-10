project(messagecenter)

set(messagecenter_SOURCES
    messagecenterengine.cpp
    messagecenter.cpp
    messagecentersettings.cpp
    messagecenterplugin.cpp
    messagecenterwebapiadapter.cpp
)

set(messagecenter_HEADERS
    messagecenterengine.h
    messagecenter.h
    messagecentersettings.h
    messagecenterplugin.h
    messagecenterwebapiadapter.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client
)

if(NOT SERVER_MODE)
    set(messagecenter_SOURCES
        ${messagecenter_SOURCES}
        messagecentergui.cpp
    )
    set(messagecenter_HEADERS
        ${messagecenter_HEADERS}
        messagecentergui.h
    )

    set(TARGET_NAME ${PLUGINS_PREFIX}featuremessagecenter)
    set(TARGET_LIB "Qt::Widgets")
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}featuremessagecentersrv)
    set(TARGET_LIB "")
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${messagecenter_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME MessageCenterPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${messagecenter_SOURCES})
endif()

if(NOT BUILD_SHARED_LIBS)
    set_property(GLOBAL APPEND PROPERTY STATIC_PLUGINS_PROPERTY ${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    Qt::Core
    ${TARGET_LIB}
    sdrbase
    ${TARGET_LIB_GUI}
)

install(TARGETS ${TARGET_NAME} DESTINATION ${INSTALL_FOLDER})

if (WIN32)
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> CONFIGURATIONS Debug RelWithDebInfo DESTINATION ${INSTALL_FOLDER})
endif()

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
