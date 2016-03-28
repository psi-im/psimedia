# add -fPIC compilation flag
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

pkg_search_module(speexdsp REQUIRED speexdsp)
pkg_search_module(gstreamer REQUIRED gstreamer-0.10)

set(SOURCES
	speexdsp.c
	speexechoprobe.c
    ../static/speexdsp_static.c)

include_directories(${speexdsp_INCLUDE_DIRS} ${gstreamer_INCLUDE_DIRS})
add_library(speexdsp OBJECT ${SOURCES})
set(PARTS_LDFLAGS ${PARTS_LDFLAGS} ${speexdsp_LDFLAGS} ${gstreamer_LDFLAGS} PARENT_SCOPE)
