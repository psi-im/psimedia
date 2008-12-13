HEADERS += \
	$$PWD/speexdsp/speexdsp.h \
	$$PWD/speexdsp/speexechoprobe.h

SOURCES += \
	$$PWD/speexdsp/speexechoprobe.c

gstplugin:SOURCES += $$PWD/speexdsp/speexdsp.c
!gstplugin:SOURCES += $$PWD/static/speexdsp_static.c

LIBS *= \
	-lspeexdsp
