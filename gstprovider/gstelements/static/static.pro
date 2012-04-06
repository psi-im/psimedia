TEMPLATE = lib
CONFIG -= qt
CONFIG += staticlib create_prl
TARGET = gstelements_static
DESTDIR = lib

CONFIG += videomaxrate liveadder speexdsp
windows:CONFIG += directsound winks
mac:CONFIG += osxaudio osxvideo

*-g++*:QMAKE_CFLAGS_WARN_ON = -Wall -Wdeclaration-after-statement #-Werror
include(../../gstconf.pri)

videomaxrate {
	include(../videomaxrate.pri)
	DEFINES += HAVE_VIDEOMAXRATE
}

liveadder {
	include(../liveadder.pri)
	DEFINES += HAVE_LIVEADDER
}

speexdsp {
	include(../speexdsp.pri)
	DEFINES += HAVE_SPEEXDSP
}

directsound {
	include(../directsound.pri)
	DEFINES += HAVE_DIRECTSOUND
}

winks {
	include(../winks.pri)
	DEFINES += HAVE_WINKS
}

osxaudio {
	include(../osxaudio.pri)
	DEFINES += HAVE_OSXAUDIO
}

osxvideo {
	include(../osxvideo.pri)
	DEFINES += HAVE_OSXVIDEO
}

HEADERS += gstelements.h
SOURCES += gstelements.c
