QMAKE_CFLAGS_WARN_ON = -Wall -Wdeclaration-after-statement -Werror
include(../../gstconf.pri)

DEFINES += HAVE_CONFIG_H
INCLUDEPATH += $$PWD
