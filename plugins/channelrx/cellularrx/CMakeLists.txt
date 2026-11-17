project(cellularrx)

set(cellularrx_SOURCES
    cellularrx.cpp
    cellularrxsettings.cpp
    cellularrxbaseband.cpp
    cellularrxplugin.cpp
)

set(cellularrx_HEADERS
    cellularrx.h
    cellularrxsettings.h
    cellularrxbaseband.h
    cellularrxplugin.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client
    ${CMAKE_SOURCE_DIR}/modemcellular
)

if(NOT SERVER_MODE)
    set(cellularrx_SOURCES
        ${cellularrx_SOURCES}
        cellularrxgui.cpp
    )
    set(cellularrx_HEADERS
        ${cellularrx_HEADERS}
        cellularrxgui.h
    )

    set(TARGET_NAME ${PLUGINS_PREFIX}cellularrx)
    set(TARGET_LIB "Qt::Widgets")
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}cellularrxsrv)
    set(TARGET_LIB "")
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${cellularrx_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME CellularRxPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${cellularrx_SOURCES})
endif()

if(NOT BUILD_SHARED_LIBS)
    set_property(GLOBAL APPEND PROPERTY STATIC_PLUGINS_PROPERTY ${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    Qt::Core
    ${TARGET_LIB}
    sdrbase
    ${TARGET_LIB_GUI}
    modemcellular
)

install(TARGETS ${TARGET_NAME} DESTINATION ${INSTALL_FOLDER})

# Install debug symbols
if (WIN32)
    install(FILES $<TARGET_PROPERTY:${TARGET_NAME},RUNTIME_OUTPUT_DIRECTORY>/${TARGET_NAME}stripped.pdb CONFIGURATIONS Release DESTINATION ${INSTALL_FOLDER} RENAME ${TARGET_NAME}.pdb )
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> CONFIGURATIONS Debug RelWithDebInfo DESTINATION ${INSTALL_FOLDER} )
endif()
