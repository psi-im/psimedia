HEADERS += \
	$$PWD/psimedia.h \
	$$PWD/psimediaprovider.h

SOURCES += \
	$$PWD/psimedia.cpp

unix {
   QMAKE_CXXFLAGS += $$(CXXFLAGS)
   QMAKE_LFLAGS += $$(LDFLAGS)
}
