TEMPLATE = lib
CONFIG -= qt
CONFIG += plugin

INCLUDEPATH += \
	/opt/local/include/glib-2.0 \
	/opt/local/lib/glib-2.0/include \
	/opt/local/include/libxml2 \
	/opt/local/include/gstreamer-0.10
LIBS += -L/opt/local/lib -lgstreamer-0.10 -lgthread-2.0 -lglib-2.0 -lgobject-2.0 -lgstvideo-0.10 -lgstbase-0.10 -lgstinterfaces-0.10

include(../../osxvideo.pri)
