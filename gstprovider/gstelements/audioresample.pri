HEADERS += \
	$$PWD/audioresample/gstaudioresample.h \
	$$PWD/audioresample/functable.h \
	$$PWD/audioresample/resample.h \
	$$PWD/audioresample/debug.h \
	$$PWD/audioresample/buffer.h

SOURCES += \
	$$PWD/audioresample/functable.c \
	$$PWD/audioresample/resample.c \
	$$PWD/audioresample/resample_functable.c \
	$$PWD/audioresample/resample_ref.c \
	$$PWD/audioresample/resample_chunk.c \
	$$PWD/audioresample/resample.h \
	$$PWD/audioresample/buffer.c

gstplugin:SOURCES += $$PWD/audioresample/gstaudioresample.c
!gstplugin:SOURCES += $$PWD/static/audioresample_static.c

#liboil
#LIBS *= \
#        -lgstaudio-0.10
