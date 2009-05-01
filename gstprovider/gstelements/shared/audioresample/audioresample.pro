TEMPLATE = lib
CONFIG -= qt
CONFIG += plugin gstplugin
DESTDIR = $$PWD/../lib
TARGET = legacyresample

include(../shared.pri)
include(../../audioresample.pri)
