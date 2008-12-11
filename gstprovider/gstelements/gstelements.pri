include(rtpmanager.pri)
include(videomaxrate.pri)

windows {
	include(directsound.pri)
	include(winks.pri)
}

mac {
	include(osxaudio.pri)
	include(osxvideo.pri)
}

HEADERS += $$PWD/gstelements.h
SOURCES += $$PWD/gstelements.c
