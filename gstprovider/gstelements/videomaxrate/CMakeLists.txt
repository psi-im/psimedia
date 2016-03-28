# add -fPIC compilation flag
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

pkg_search_module(gstreamer REQUIRED gstreamer-0.10)

set(SOURCES
	videomaxrate.c
    ../static/videomaxrate_static.c)

include_directories(${gstreamer_INCLUDE_DIRS})
add_library(videomaxrate OBJECT ${SOURCES})
set(PARTS_LDFLAGS ${PARTS_LDFLAGS} ${gstreamer_LDFLAGS} PARENT_SCOPE)
