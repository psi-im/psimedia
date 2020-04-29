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
QMAKE_CXXFLAGS_DEBUG-=-O2
QMAKE_CXXFLAGS_RELEASE -= -O1
QMAKE_CXXFLAGS_RELEASE -= -O2

QMAKE_CXXFLAGS_RELEASE+=-O0
QMAKE_CXXFLAGS_DEBUG+=-O0
QMAKE_CXXFLAGS+=-O0
