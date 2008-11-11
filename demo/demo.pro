CONFIG -= app_bundle
QT += network

include(../src/psimedia.pri)
INCLUDEPATH += ../src

include(../gstprovider/gstprovider.pri)

SOURCES += main.cpp

FORMS += mainwin.ui config.ui
