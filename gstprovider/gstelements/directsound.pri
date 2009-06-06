HEADERS += \
	$$PWD/directsound/gstdirectsound.h \
	$$PWD/directsound/gstdirectsoundringbuffer.h \
	$$PWD/directsound/gstdirectsoundsink.h \
	$$PWD/directsound/gstdirectsoundsrc.h

SOURCES += \
	$$PWD/directsound/gstdirectsound.c \
	$$PWD/directsound/gstdirectsoundringbuffer.c \
	$$PWD/directsound/gstdirectsoundsink.c \
	$$PWD/directsound/gstdirectsoundsrc.c

gstplugin:SOURCES += $$PWD/directsound/gstdirectsoundplugin.c
!gstplugin:SOURCES += $$PWD/static/directsound_static.c

LIBS *= \
	-lgstinterfaces-0.10 \
	-lgstaudio-0.10 \
	-ldsound \
	-ldxerr9
