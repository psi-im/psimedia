project(gstprovider)

cmake_minimum_required(VERSION 2.8.11)

#Detect MXE cross-compilation
if( (CMAKE_CROSSCOMPILING) AND (DEFINED MSYS) )
    message(STATUS "MXE environment detected")
    option(USE_MXE "Use MXE toolkit" ON)
    message(STATUS "MXE root path: ${CMAKE_PREFIX_PATH}")
endif()

# On Windows debug library should have 'd' postfix.
if(WIN32)
    set(CMAKE_DEBUG_POSTFIX "d")
    if(NOT USE_MXE)
        if(CMAKE_SIZEOF_VOID_P MATCHES "8")
            set(GST_SDK $ENV{GSTREAMER_1_0_ROOT_x86_64})
        else()
            set(GST_SDK $ENV{GSTREAMER_1_0_ROOT_x86})
        endif()
        if(GST_SDK)
            set(GST_BIN_DIR "${GST_SDK}/bin")
            set(GST_LIB_DIR "${GST_SDK}/lib")
            set(GST_INC_DIR "${GST_SDK}/include")
            include_directories(
                ${GST_LIB_DIR}
                ${GST_INC_DIR}
            )
            link_directories(
                ${GST_BIN_DIR}
                ${GST_LIB_DIR}
            )
            #try to find pkg-config executable in GST_BIN_DIR
            find_program(PKG_EXEC pkg-config PATHS ${GST_BIN_DIR})
            if(NOT "${PKG_EXEC}" STREQUAL "PKG_EXEC-NOTFOUND")
                set(PKG_CONFIG_EXECUTABLE ${PKG_EXEC})
            endif()
        endif()
    endif()
elseif(APPLE)
    set(CMAKE_DEBUG_POSTFIX "_debug")
endif(WIN32)

find_package(PkgConfig REQUIRED)

option(USE_PSI "Use gstprovider module for Psi client. Should be disabled for Psi+ client" ON)
option(BUILD_DEMO "Build psimedia-demo" ON)

if(USE_PSI)
    set(CLIENT_NAME "psi")
else()
    set(CLIENT_NAME "psi-plus")
endif()

message(STATUS "${PROJECT_NAME} module will be installed in plugins directory for client: \"${CLIENT_NAME}\"")

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

#add extra search paths for libraries and includes
if(WIN32)
    set(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}" CACHE STRING "Directory where binary will be installed")
    set(LIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/plugins" CACHE STRING "Directory where plugin will be installed")
else(WIN32)
    set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
    set(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE STRING "Directory where binary will be installed")
    set(LIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/${CLIENT_NAME}/plugins" CACHE STRING "Directory where plugin will be installed")
endif(WIN32)

if(BUILD_DEMO)
  add_subdirectory(demo)
endif()
add_subdirectory(gstprovider)
