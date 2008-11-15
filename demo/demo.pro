CONFIG -= app_bundle
QT += network

include(../psimedia/psimedia.pri)
INCLUDEPATH += ../psimedia

DEFINES += GSTPROVIDER_STATIC
DEFINES += QT_STATICPLUGIN
include(../gstprovider/gstprovider.pri)

SOURCES += main.cpp

FORMS += mainwin.ui config.ui
