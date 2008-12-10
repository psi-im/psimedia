include(gstconf.pri)

include(deviceenum/deviceenum.pri)
include(gstcustomelements/gstcustomelements.pri)
include(gstelements/gstelements.pri)

HEADERS += \
	$$PWD/devices.h \
	$$PWD/modes.h \
	$$PWD/payloadinfo.h \
	$$PWD/rtpworker.h \
	$$PWD/gstthread.h \
	$$PWD/rwcontrol.h

SOURCES += \
	$$PWD/devices.cpp \
	$$PWD/modes.cpp \
	$$PWD/payloadinfo.cpp \
	$$PWD/rtpworker.cpp \
	$$PWD/gstthread.cpp \
	$$PWD/rwcontrol.cpp \
	$$PWD/gstprovider.cpp
