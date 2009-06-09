# FIXME: building elements in shared mode causes them to drag in the entire
#   dependencies of psimedia

include(../conf.pri)

windows {
	INCLUDEPATH += \
		c:/mingw/include/glib-2.0 \
		c:/mingw/lib/glib-2.0/include \
		c:/msys/1.0/home/gst/include \
		c:/msys/1.0/home/gst/include/liboil-0.3 \
		c:/msys/1.0/home/gst/include/gstreamer-0.10
	LIBS += \
		-Lc:/msys/1.0/home/gst/lib \
		-lgstreamer-0.10.dll \
		-lgthread-2.0 \
		-lglib-2.0 \
		-lgobject-2.0 \
		-lgstvideo-0.10.dll \
		-lgstbase-0.10.dll \
		-lgstinterfaces-0.10.dll

	# qmake mingw seems to have broken prl support, so force these
	win32-g++|contains($$list($$[QT_VERSION]), 4.0.*|4.1.*|4.2.*|4.3.*) {
		LIBS *= \
			-loil-0.3 \
			-lgstaudio-0.10.dll \
			-lgstrtp-0.10.dll \
			-lgstnetbuffer-0.10.dll \
			-lspeexdsp.dll \
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
