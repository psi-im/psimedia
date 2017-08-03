# add -fPIC compilation flag
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

pkg_search_module(audio REQUIRED gstreamer-audio-0.10)
pkg_search_module(interfaces REQUIRED gstreamer-interfaces-0.10)

set(SOURCES
	gstdirectsound.c
	gstdirectsoundringbuffer.c
	gstdirectsoundsink.c
	gstdirectsoundsrc.c
	../static/directsound_static.c)

include_directories(${audio_INCLUDE_DIRS} ${interfaces_INCLUDE_DIRS})
add_library(directsound OBJECT ${SOURCES})
set(PARTS_LDFLAGS ${PARTS_LDFLAGS} ${audio_LDFLAGS} ${interfaces_LDFLAGS} -ldsound -ldxerr9 -lole32 -ldxguid PARENT_SCOPE)
