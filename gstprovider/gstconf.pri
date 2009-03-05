windows {
	INCLUDEPATH += \
		c:/mingw/include/glib-2.0 \
		c:/mingw/lib/glib-2.0/include \
		c:/msys/1.0/home/gst/include \
		c:/msys/1.0/home/gst/include/gstreamer-0.10
	LIBS += \
		-Lc:/msys/1.0/home/gst/lib -llibgstreamer-0.10 -lgthread-2.0 -lglib-2.0 -lgobject-2.0 -llibgstvideo-0.10 -llibgstbase-0.10 -llibgstinterfaces-0.10
}

unix {
	include(../conf.pri)

	LIBS += -lgstvideo-0.10 -lgstinterfaces-0.10
}
