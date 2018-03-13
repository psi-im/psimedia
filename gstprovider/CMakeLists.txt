cmake_minimum_required(VERSION 2.8.11)

find_package(Qt5 COMPONENTS Core Widgets REQUIRED)

if(Qt5Core_FOUND)
    message(STATUS "Qt5 found (${Qt5Core_VERSION})")
endif()

# Tell CMake to run moc when necessary:
set(CMAKE_AUTOMOC ON)
# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

pkg_search_module(GLIB2 REQUIRED glib-2.0)
pkg_search_module(GOBJECT2 REQUIRED gobject-2.0)
pkg_search_module(GTHREAD2 REQUIRED gthread-2.0)
pkg_search_module(GSTVIDEO REQUIRED gstreamer-video-1.0)
pkg_search_module(GSTBASE REQUIRED gstreamer-base-1.0)
pkg_search_module(GSTAUDIO REQUIRED gstreamer-audio-1.0)
pkg_search_module(GSTAPP REQUIRED gstreamer-app-1.0)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0)

message(STATUS "glib-2.0 module found (${GLIB2_VERSION})")
message(STATUS "gobject-2.0 module found (${GOBJECT2_VERSION})")
message(STATUS "gthread-2.0 module found (${GTHREAD2_VERSION})")
message(STATUS "gstreamer-video-1.0 module found (${GSTVIDEO_VERSION})")
message(STATUS "gstreamer-base-1.0 module found (${GSTBASE_VERSION})")
message(STATUS "gstreamer-audio-1.0 module found (${GSTAUDIO_VERSION})")
message(STATUS "gstreamer-app-1.0 module found (${GSTAPP_VERSION})")
message(STATUS "gstreamer-1.0 module found (${GSTREAMER_VERSION})")

set(SOURCES
    devices.cpp
    modes.cpp
    payloadinfo.cpp
    pipeline.cpp
    bins.cpp
    rtpworker.cpp
    gstthread.cpp
    rwcontrol.cpp
    gstprovider.cpp
)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES devices/deviceenum_unix.cpp)
endif()


set(GST_INCLUDES
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTAPP_INCLUDE_DIRS}
    ${GSTAUDIO_INCLUDE_DIRS}
    ${GSTBASE_INCLUDE_DIRS}
    ${GSTVIDEO_INCLUDE_DIRS}
    ${GTHREAD2_INCLUDE_DIRS}
    ${GOBJECT2_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
)

set(EXTRA_LDFLAGS
    ${GSTREAMER_LDFLAGS}
    ${GSTAPP_LDFLAGS}
    ${GSTAUDIO_LDFLAGS}
    ${GSTBASE_LDFLAGS}
    ${GSTVIDEO_LDFLAGS}
    ${GTHREAD2_LDFLAGS}
    ${GOBJECT2_LDFLAGS}
    ${GLIB2_LDFLAGS}
    "-Wl,-as-needed"
)

if(WIN32)
    list(APPEND EXTRA_LDFLAGS
        -loil-0.3-0
        -lgstaudio-1.0-0
        -lgstrtp-1.0-0
        -lgstnetbuffer-1.0-0
        -lopus
        -lsetupapi
        -lksuser
        -lamstrmid
        -ldsound
        -ldxerr9
        -lole32
    )
endif()

include_directories(${GST_INCLUDES} ${CMAKE_SOURCE_DIR}/psimedia)

add_library(gstprovider MODULE ${PARTS} ${SOURCES})
target_link_libraries(${PROJECT_NAME}  ${EXTRA_LDFLAGS})

if(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework CoreAudio")
    set_property(TARGET ${PROJECT_NAME}  PROPERTY SUFFIX ".dylib")
endif()

target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Gui Qt5::Widgets)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${LIB_INSTALL_DIR})
