CONFIG += console
CONFIG -= app_bundle
QT += network

HEADERS += psimedia.h psimediaprovider.h
SOURCES += psimedia.cpp main.cpp

FORMS += mainwin.ui config.ui

DEFINES += GSTPROVIDER_STATIC

# gstreamer stuff
DEFINES += QT_STATICPLUGIN
HEADERS += gstcustomelements.h
SOURCES += \
	gstcustomelements.c \
	gstcustomelements_appvideosink.c \
	gstcustomelements_apprtpsrc.c \
	gstcustomelements_apprtpsink.c \
	gstprovider.cpp

INCLUDEPATH += \
	/usr/include/glib-2.0 \
	/usr/lib/glib-2.0/include \
	/usr/include/libxml2 \
	/usr/include/gstreamer-0.10
LIBS += -lgstreamer-0.10 -lgthread-2.0 -lglib-2.0 -lrt -lgstvideo-0.10

# device enum
windows: {
        SOURCES += deviceenum_win.cpp
}
unix:!mac: {
        SOURCES += deviceenum_unix.cpp
}
mac: {
        SOURCES += deviceenum_mac.cpp
        LIBS += -framework CoreAudio
}
