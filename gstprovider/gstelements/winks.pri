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
	$$PWD/winks/gstksvideodevice.c \
	#$$PWD/winks/gstksvideosrc.c
	$$PWD/winks_static.c

LIBS *= \
	-lsetupapi \
	-lksuser \
	-lamstrmid
