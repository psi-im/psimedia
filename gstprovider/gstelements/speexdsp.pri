HEADERS += \
	$$PWD/speexdsp/speexdsp.h \
	$$PWD/speexdsp/speexechoprobe.h

SOURCES += \
	$$PWD/speexdsp/speexdsp.c \
	$$PWD/speexdsp/speexechoprobe.c

gstplugin:SOURCES += $$PWD/speexdsp/speexdspplugin.c
!gstplugin:SOURCES += $$PWD/static/speexdsp_static.c

LIBS *= \
	-lspeexdsp
