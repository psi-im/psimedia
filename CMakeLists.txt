project(gstprovider)

cmake_minimum_required(VERSION 2.8.11)

find_package(PkgConfig REQUIRED)

# On Windows debug library should have 'd' postfix.
if(WIN32)
  set(CMAKE_DEBUG_POSTFIX "d")
elseif(APPLE)
  set(CMAKE_DEBUG_POSTFIX "_debug")
endif(WIN32)

#add extra search paths for libraries and includes
set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
set(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE STRING "Directory where binary will install")
set(LIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/psi/plugins" CACHE STRING "Directory where plugin will install")

option(BUILD_DEMO "Build psimedia-demo" ON)

if(BUILD_DEMO)
  add_subdirectory(demo)
endif()
add_subdirectory(gstprovider)
