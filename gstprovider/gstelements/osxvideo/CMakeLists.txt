# add -fPIC compilation flag
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

pkg_search_module(interfaces REQUIRED gstreamer-interfaces-0.10)
pkg_search_module(video REQUIRED gstreamer-video-0.10)

set(SOURCES
	osxvideosink.m
	cocoawindow.m
	osxvideosrc.c
	../static/osxvideo_static.m)

include_directories(${interfaces_INCLUDE_DIRS} ${video_INCLUDE_DIRS})
add_library(osxvideo OBJECT ${SOURCES})
set(PARTS_LDFLAGS ${PARTS_LDFLAGS} ${interfaces_LDFLAGS} ${video_LDFLAGS} "-framework Cocoa" "-framework QuickTime" "-framework OpenGL" PARENT_SCOPE)
