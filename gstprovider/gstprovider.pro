TEMPLATE = lib
CONFIG += plugin

CONFIG(debug, debug|release) {
  mac: TARGET = gstprovider_debug
  windows: TARGET = gstproviderd
}

greaterThan(QT_MAJOR_VERSION, 4) {
  QT += widgets
}

INCLUDEPATH += ../psimedia

include(gstprovider.pri)
