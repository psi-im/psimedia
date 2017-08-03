# add -fPIC compilation flag
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

pkg_search_module(interfaces REQUIRED gstreamer-interfaces-0.10)
pkg_search_module(audio REQUIRED gstreamer-audio-0.10)

set(SOURCES
	gstosxringbuffer.c
	gstosxaudioelement.c
	gstosxaudiosink.c
	gstosxaudiosrc.c
	../static/osxaudio_static.c)

include_directories(${interfaces_INCLUDE_DIRS} ${audio_INCLUDE_DIRS})
add_library(osxaudio OBJECT ${SOURCES})
set(PARTS_LDFLAGS ${PARTS_LDFLAGS} ${interfaces_LDFLAGS} ${audio_LDFLAGS} "-framework CoreAudio" "-framework AudioUnit" "-framework Carbon" PARENT_SCOPE)
