HEADERS += \
	$$PWD/videomaxrate/videomaxrate.h

SOURCES += \
	$$PWD/videomaxrate/videomaxrate.c

gstplugin:SOURCES += $$PWD/videomaxrate/videomaxrateplugin.c
!gstplugin:SOURCES += $$PWD/static/videomaxrate_static.c
