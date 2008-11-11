CONFIG -= app_bundle
QT += network

include(../psimedia/psimedia.pri)
INCLUDEPATH += ../psimedia

include(../gstprovider/gstprovider.pri)

SOURCES += main.cpp

FORMS += mainwin.ui config.ui
