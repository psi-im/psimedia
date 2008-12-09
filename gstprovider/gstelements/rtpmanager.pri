HEADERS += \
	$$PWD/rtpmanager/gstrtpbin-marshal.h \
	$$PWD/rtpmanager/gstrtpbin.h \
	$$PWD/rtpmanager/gstrtpclient.h \
	$$PWD/rtpmanager/gstrtpjitterbuffer.h \
	$$PWD/rtpmanager/gstrtpptdemux.h \
	$$PWD/rtpmanager/gstrtpssrcdemux.h \
	$$PWD/rtpmanager/rtpjitterbuffer.h \
	$$PWD/rtpmanager/rtpsession.h \
	$$PWD/rtpmanager/rtpsource.h \
	$$PWD/rtpmanager/rtpstats.h \
	$$PWD/rtpmanager/gstrtpsession.h

SOURCES += \
	#$$PWD/rtpmanager/gstrtpmanager.c \
	$$PWD/rtpmanager_static.c \
	$$PWD/rtpmanager/gstrtpbin-marshal.c \
	$$PWD/rtpmanager/gstrtpbin.c \
	$$PWD/rtpmanager/gstrtpclient.c \
	$$PWD/rtpmanager/gstrtpjitterbuffer.c \
	$$PWD/rtpmanager/gstrtpptdemux.c \
	$$PWD/rtpmanager/gstrtpssrcdemux.c \
	$$PWD/rtpmanager/rtpjitterbuffer.c \
	$$PWD/rtpmanager/rtpsession.c \
	$$PWD/rtpmanager/rtpsource.c \
	$$PWD/rtpmanager/rtpstats.c \
	$$PWD/rtpmanager/gstrtpsession.c

LIBS *= \
	-lgstnetbuffer-0.10 \
	-lgstrtp-0.10
