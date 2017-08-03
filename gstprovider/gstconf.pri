# FIXME: building elements in shared mode causes them to drag in the entire
#   dependencies of psimedia

include(../conf.pri)

windows {
	INCLUDEPATH += \
		c:/glib/include/glib-2.0 \
		c:/glib/lib/glib-2.0/include \
		c:/gstforwin/dxsdk/include \
		c:/gstforwin/winsdk/include \
		c:/gstforwin/include \
		c:/gstforwin/include/liboil-0.3 \
		c:/gstforwin/include/libxml2 \
		c:/gstforwin/gstreamer/include/gstreamer-0.10 \
		c:/gstforwin/gst-plugins-base/include/gstreamer-0.10
	LIBS += \
		-Lc:/glib/bin \
		-Lc:/gstforwin/bin \
		-Lc:/gstforwin/gstreamer/bin \
		-Lc:/gstforwin/gst-plugins-base/bin \
		-lgstreamer-0.10-0 \
		-lgthread-2.0-0 \
		-lglib-2.0-0 \
		-lgobject-2.0-0 \
		-lgstvideo-0.10-0 \
		-lgstbase-0.10-0 \
		-lgstinterfaces-0.10-0

	# qmake mingw seems to have broken prl support, so force these
	win32-g++|contains($$list($$[QT_VERSION]), 4.0.*|4.1.*|4.2.*|4.3.*) {
		LIBS *= \
			-Lc:/gstforwin/winsdk/lib \
			-loil-0.3-0 \
			-lgstaudio-0.10-0 \
			-lgstrtp-0.10-0 \
			-lgstnetbuffer-0.10-0 \
			-lspeexdsp-1 \
			-lsetupapi \
			-lksuser \
			-lamstrmid \
			-ldsound \
			-ldxerr9 \
			-lole32
	}
}

unix {
	LIBS += -lgstvideo-0.10 -lgstinterfaces-0.10
}
