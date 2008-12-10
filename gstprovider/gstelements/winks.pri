# winks
HEADERS += \
	$$PWD/winks/kshelpers.h \
	$$PWD/winks/ksvideohelpers.h \
	$$PWD/winks/gstksclock.h \
	$$PWD/winks/gstksvideodevice.h \
	$$PWD/winks/gstksvideosrc.h

SOURCES += \
	$$PWD/winks/kshelpers.c \
	$$PWD/winks/ksvideohelpers.c \
	$$PWD/winks/gstksclock.c \
	$$PWD/winks/gstksvideodevice.c

gstplugin:SOURCES += $$PWD/winks/gstksvideosrc.c
!gstplugin:SOURCES += $$PWD/static/winks_static.c

LIBS *= \
	-lsetupapi \
	-lksuser \
	-lamstrmid
