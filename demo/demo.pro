CONFIG -= app_bundle
QT += network

greaterThan(QT_MAJOR_VERSION, 4) {
  QT += widgets
}

CONFIG += debug

CONFIG(debug, debug|release) {
  mac: DEFINES += DEBUG_POSTFIX=\\\"_debug\\\"
  else:windows: DEFINES += DEBUG_POSTFIX=\\\"d\\\"
  else: DEFINES += DEBUG_POSTFIX=\\\"\\\"
}else {
  DEFINES += DEBUG_POSTFIX=\\\"\\\"
}

include(../psimedia/psimedia.pri)
INCLUDEPATH += ../psimedia

#DEFINES += GSTPROVIDER_STATIC
#DEFINES += QT_STATICPLUGIN
#include(../gstprovider/gstprovider.pri)

SOURCES += main.cpp

FORMS += mainwin.ui config.ui
