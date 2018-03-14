cmake_minimum_required(VERSION 2.8.11)

find_package(Qt5 COMPONENTS Core Widgets REQUIRED)

if(Qt5Core_FOUND)
    message(STATUS "Qt5 found, version ${Qt5Core_VERSION}")
endif()

# Tell CMake to run moc when necessary:
set(CMAKE_AUTOMOC ON)
# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

#search Glib modules
pkg_check_modules(GLIBMODULES REQUIRED
                    glib-2.0
                    gobject-2.0
                    gthread-2.0
)
#search Gstreamer modules
pkg_check_modules(GSTMODULES REQUIRED
                    gstreamer-1.0
                    gstreamer-app-1.0
                    gstreamer-base-1.0
                    gstreamer-audio-1.0
                    gstreamer-video-1.0
)

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
    ${GSTMODULES_INCLUDE_DIRS}
    ${GLIBMODULES_INCLUDE_DIRS}
)

set(PROJECT_LDFLAGS
    ${GSTMODULES_LDFLAGS}
    ${GLIBMODULES_LDFLAGS}
    "-Wl,-as-needed"
)


if(WIN32)
    list(APPEND PROJECT_LDFLAGS
        oil-0.3
        gstaudio-1.0
        gstrtp-1.0
        gstnet-1.0
        opus
        setupapi
        ksuser
        amstrmid
        dsound
        dxerr9
        ole32
    )
endif()

message(STATUS "INCLUDE_DIRS=\"${GST_INCLUDES}\"")
message(STATUS "LDFLAGS=\"${PROJECT_LDFLAGS}\"")

include_directories(${GST_INCLUDES} ${CMAKE_SOURCE_DIR}/psimedia)

add_library(gstprovider MODULE ${SOURCES})
target_link_libraries(${PROJECT_NAME} ${PROJECT_LDFLAGS})

if(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework CoreAudio")
    set_property(TARGET ${PROJECT_NAME}  PROPERTY SUFFIX ".dylib")
endif()

target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Gui Qt5::Widgets)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${LIB_INSTALL_DIR})
