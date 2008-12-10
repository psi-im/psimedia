HEADERS += \
	$$PWD/directsound/gstdirectsound.h \
	$$PWD/directsound/gstdirectsoundsrc.h \
	$$PWD/directsound/gstdirectsoundsink.h

SOURCES += \
	$$PWD/directsound/gstdirectsound.c \
	$$PWD/directsound/gstdirectsoundsrc.c \
	$$PWD/directsound/gstdirectsoundsink.c

gstplugin:SOURCES += $$PWD/directsound/gstdirectsoundplugin.c
!gstplugin:SOURCES += $$PWD/static/directsound_static.c

LIBS *= \
	-lgstinterfaces-0.10 \
	-lgstaudio-0.10 \
	-ldsound \
	-ldxerr9
