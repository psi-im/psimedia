windows {
	INCLUDEPATH += \
		c:/mingw/include/glib-2.0 \
		c:/mingw/lib/glib-2.0/include \
		c:/msys/1.0/home/gst/include/gstreamer-0.10
	LIBS += \
		-Lc:/msys/1.0/home/gst/lib -llibgstreamer-0.10 -lgthread-2.0 -lglib-2.0 -lgobject-2.0 -llibgstvideo-0.10 -llibgstbase-0.10 -llibgstinterfaces-0.10
}

unix:!mac {
	INCLUDEPATH += \
		/usr/include/glib-2.0 \
		/usr/lib/glib-2.0/include \
		/usr/include/libxml2 \
		/usr/include/gstreamer-0.10
	LIBS += -lgstreamer-0.10 -lgthread-2.0 -lglib-2.0 -lrt -lgstvideo-0.10 -lgstinterfaces-0.10
}

mac {
	INCLUDEPATH += \
		/opt/local/include/glib-2.0 \
		/opt/local/lib/glib-2.0/include \
		/opt/local/include/libxml2 \
		/opt/local/include/gstreamer-0.10
	LIBS += -L/opt/local/lib -lgstreamer-0.10 -lgthread-2.0 -lglib-2.0 -lgobject-2.0 -lgstvideo-0.10 -lgstbase-0.10 -lgstinterfaces-0.10
}
