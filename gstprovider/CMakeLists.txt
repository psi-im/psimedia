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

add_definitions(
    ${GLIBMODULES_CFLAGS}
    ${GLIBMODULES_CFLAGS_OTHER}
    ${GSTMODULES_CFLAGS}
    ${GSTMODULES_CFLAGS_OTHER}
)

link_directories(
    ${GSTMODULES_LIBRARY_DIRS}
    ${GLIBMODULES_LIBRARY_DIRS}
)

set(GST_INCLUDES
    ${GSTMODULES_INCLUDE_DIRS}
    ${GLIBMODULES_INCLUDE_DIRS}
)

set(CMAKE_MODULE_LINKER_FLAGS
    ${GLIBMODULES_LDFLAGS_OTHER}
    ${GSTMODULES_LDFLAGS_OTHER}
)

if(WIN32)
    list(APPEND PROJECT_LDFLAGS
        gstaudio-1.0
        gstrtp-1.0
        opus
        setupapi
        ksuser
        dsound
        ole32
    )
    if(USE_MXE OR MSVC)
        list(APPEND PROJECT_LDFLAGS
            amstrmid
        )
    endif()
    if(NOT MSVC)
        list(APPEND PROJECT_LDFLAGS
            dxerr9
        )
    endif()
endif()

include_directories(${GST_INCLUDES} ${CMAKE_SOURCE_DIR}/psimedia)

add_library(gstprovider MODULE ${SOURCES})
target_link_libraries(${PROJECT_NAME}
                      ${GSTMODULES_LIBRARIES}
                      ${GLIBMODULES_LIBRARIES}
                      ${PROJECT_LDFLAGS}
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework CoreAudio")
    set_property(TARGET ${PROJECT_NAME}  PROPERTY SUFFIX ".dylib")
endif()

target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Gui Qt5::Widgets)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${LIB_INSTALL_DIR})
