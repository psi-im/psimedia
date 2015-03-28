TEMPLATE = lib
CONFIG += plugin

greaterThan(QT_MAJOR_VERSION, 4) {
  QT += widgets
}

INCLUDEPATH += ../psimedia

include(gstprovider.pri)
