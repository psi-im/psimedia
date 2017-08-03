# add -fPIC compilation flag
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

pkg_search_module(glib REQUIRED glib-2.0)
pkg_search_module(gstreamer REQUIRED gstreamer-0.10)
set(SOURCES
	kshelpers.c
	ksvideohelpers.c
	gstksclock.c
	gstksvideodevice.c
	../static/winks_static.c)

include_directories(${glib_INCLUDE_DIRS} ${gstreamer_INCLUDE_DIRS})
add_library(winks OBJECT ${SOURCES})
set(PARTS_LDFLAGS ${PARTS_LDFLAGS} ${glib_LDFLAGS} ${gstreamer_LDFLAGS} -lsetupapi -lksuser -lstrmiids PARENT_SCOPE)
