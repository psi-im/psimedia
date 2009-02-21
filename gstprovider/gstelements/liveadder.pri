HEADERS += \
	$$PWD/liveadder/liveadder.h

gstplugin:SOURCES += $$PWD/liveadder/liveadder.c
!gstplugin:SOURCES += $$PWD/static/liveadder_static.c

LIBS *= \
	-lgstaudio-0.10
