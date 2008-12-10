HEADERS += \
	$$PWD/osxaudio/gstosxaudiosink.h \
	$$PWD/osxaudio/gstosxaudioelement.h \
	$$PWD/osxaudio/gstosxringbuffer.h \
	$$PWD/osxaudio/gstosxaudiosrc.h

SOURCES += \
	$$PWD/osxaudio/gstosxringbuffer.c \
	$$PWD/osxaudio/gstosxaudioelement.c \
	$$PWD/osxaudio/gstosxaudiosink.c \
	$$PWD/osxaudio/gstosxaudiosrc.c

gstplugin:SOURCES += $$PWD/osxaudio/gstosxaudio.c
!gstplugin:SOURCES += $$PWD/static/osxaudio_static.c

LIBS *= \
	-lgstinterfaces-0.10 \
	-lgstaudio-0.10

LIBS += \
	-framework CoreAudio \
	-framework AudioUnit \
	-framework Carbon
