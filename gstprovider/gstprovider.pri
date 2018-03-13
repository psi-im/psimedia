CONFIG += link_prl

include(gstconf.pri)
include(devices/deviceenum.pri)

HEADERS += \
	$$PWD/devices.h \
	$$PWD/modes.h \
	$$PWD/payloadinfo.h \
	$$PWD/pipeline.h \
	$$PWD/bins.h \
	$$PWD/rtpworker.h \
	$$PWD/gstthread.h \
	$$PWD/rwcontrol.h

SOURCES += \
	$$PWD/devices.cpp \
	$$PWD/modes.cpp \
	$$PWD/payloadinfo.cpp \
	$$PWD/pipeline.cpp \
	$$PWD/bins.cpp \
	$$PWD/rtpworker.cpp \
	$$PWD/gstthread.cpp \
	$$PWD/rwcontrol.cpp \
	$$PWD/gstprovider.cpp

unix {
   QMAKE_CXXFLAGS += $$(CXXFLAGS)
   QMAKE_LFLAGS += $$(LDFLAGS)
}
