TEMPLATE = lib
CONFIG -= qt
CONFIG += plugin gstplugin
DESTDIR = $$PWD/../lib

include(../../../gstconf.pri)

DEFINES += HAVE_CONFIG_H
INCLUDEPATH += $$PWD/..

include(../../directsound.pri)
