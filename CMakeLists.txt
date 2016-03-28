project(gstprovider)

cmake_minimum_required(VERSION 2.8.11)

find_package(PkgConfig REQUIRED)

# On Windows debug library should have 'd' postfix.
if(WIN32)
  set(CMAKE_DEBUG_POSTFIX "d")
elseif(APPLE)
  set(CMAKE_DEBUG_POSTFIX "_debug")
endif(WIN32)

option(QT4_BUILD "Force building with Qt4 even if Qt5 is found" OFF)

option(ENABLE_VIDEOMAXRATE "" ON)
option(ENABLE_LIVEADDER "" ON)
option(ENABLE_SPEEXDSP "" ON)

# Windows specific features
if(WIN32)
  option(ENABLE_DIRECTSOUND "" ON)
  option(ENABLE_WINKS "" ON)
endif()

# Mac OS specific features
if(APPLE)
  option(ENABLE_OSXAUDIO "" ON)
  option(ENABLE_OSXVIDIO "" ON)
endif()

if(ENABLE_VIDEOMAXRATE)
  add_definitions(-DHAVE_VIDEOMAXRATE)
  add_subdirectory(gstprovider/gstelements/videomaxrate)
  list(APPEND PARTS $<TARGET_OBJECTS:videomaxrate>)
endif()

if(ENABLE_LIVEADDER)
  add_definitions(-DHAVE_LIVEADDER)
  add_subdirectory(gstprovider/gstelements/liveadder)
  list(APPEND PARTS $<TARGET_OBJECTS:liveadder>)
endif()

if(ENABLE_SPEEXDSP)
  add_definitions(-DHAVE_SPEEXDSP)
  add_subdirectory(gstprovider/gstelements/speexdsp)
  list(APPEND PARTS $<TARGET_OBJECTS:speexdsp>)
endif()

if(ENABLE_DIRECTSOUND)
  add_definitions(-DHAVE_DIRECTSOUND)
  add_subdirectory(gstprovider/gstelements/directsound)
  list(APPEND PARTS $<TARGET_OBJECTS:directsound>)
endif()

if(ENABLE_WINKS)
  add_definitions(-DHAVE_WINKS)
  add_subdirectory(gstprovider/gstelements/winks)
  list(APPEND PARTS $<TARGET_OBJECTS:winks>)
endif()

if(ENABLE_OSXAUDIO)
  add_definitions(-DHAVE_OSXAUDIO)
  add_subdirectory(gstprovider/gstelements/osxaudio)
  list(APPEND PARTS $<TARGET_OBJECTS:osxaudio>)
endif()

if(ENABLE_OSXVIDIO)
  add_definitions(-DHAVE_OSXVIDIO)
  add_subdirectory(gstprovider/gstelements/osxvideo)
  list(APPEND PARTS $<TARGET_OBJECTS:osxvideo>)
endif()

add_subdirectory(gstprovider)
