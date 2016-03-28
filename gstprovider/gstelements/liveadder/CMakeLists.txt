# add -fPIC compilation flag
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

pkg_search_module(audio REQUIRED gstreamer-audio-0.10)

set(SOURCES
    ../static/liveadder_static.c)

include_directories(${audio_INCLUDE_DIRS})
add_library(liveadder OBJECT ${SOURCES})
set(PARTS_LDFLAGS ${PARTS_LDFLAGS} ${audio_LDFLAGS} PARENT_SCOPE)
