unset(_HDRS)
unset(_UIS)
unset(_SRCS)
unset(_RSCS)
unset(PLUGIN)

set( PLUGIN mediaplugin )
project(${PLUGIN} LANGUAGES CXX)
cmake_minimum_required(VERSION 3.10.0)
if(POLICY CMP0071)
    cmake_policy(SET CMP0071 NEW)
endif()
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()
set( CMAKE_AUTOMOC TRUE )

get_filename_component(ABS_PLUGINS_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PLUGINS_ROOT_DIR "${ABS_PLUGINS_ROOT_DIR}" CACHE STRING "Plugins root path. Path where include directory placed")

if(NOT BUILD_DEMO)
    include_directories(
        ${ABS_PLUGINS_ROOT_DIR}/gstprovider
        ${ABS_PLUGINS_ROOT_DIR}/gstprovider/devices
    )
endif()

set( CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${ABS_PLUGINS_ROOT_DIR}/cmake/modules
    ${PLUGINS_ROOT_DIR}/cmake/modules
)

find_package(PsiPluginsApi REQUIRED)
include(${PsiPluginsApi_DIR}/variables.cmake)
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PsiPluginsApi_INCLUDE_DIR}
)

if(NOT PLUGINS_INSTALL_PATH)
    set(PLUGINS_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/${PLUGINS_PATH}")
endif()

set( _SRCS
    psiplugin.cpp
    opt_avcall.cpp
    opt_avcall.h
)

set(FORMS opt_avcall.ui)

find_package( Qt${QT_DEFAULT_MAJOR_VERSION} COMPONENTS Widgets REQUIRED )
list(APPEND QT_DEPLIBS
    Qt${QT_DEFAULT_MAJOR_VERSION}::Widgets
)
qt_wrap_ui(UIS ${FORMS})

if(WIN32)
    set(LIB_TYPE "MODULE")
else()
    set(LIB_TYPE "SHARED")
endif()

add_library(
    ${PLUGIN}
    ${LIB_TYPE}
    ${_SRCS}
    ${UIS}
    #${RSCS}
)

add_dependencies(${PLUGIN} gstprovidersrc)

target_link_libraries(
    ${PLUGIN}
    PRIVATE
    gstprovidersrc
)

target_link_libraries(${PLUGIN} PRIVATE ${QT_DEPLIBS})

install(
    TARGETS
    ${PLUGIN}
    LIBRARY
    DESTINATION
    ${PLUGINS_INSTALL_PATH}
    RUNTIME DESTINATION
    ${PLUGINS_INSTALL_PATH}
)
