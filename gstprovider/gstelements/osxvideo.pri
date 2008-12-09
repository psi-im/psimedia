HEADERS += \
	$$PWD/osxvideo/osxvideosink.h \
	$$PWD/osxvideo/cocoawindow.h \
	$$PWD/osxvideo/osxvideosrc.h

SOURCES += \
	#$$PWD/osxvideo/osxvideoplugin.m \
	$$PWD/osxvideo/osxvideo_static.m \
	$$PWD/osxvideo/osxvideosink.m \
	$$PWD/osxvideo/cocoawindow.m \
	$$PWD/osxvideo/osxvideosrc.c

LIBS *= \
	-lgstinterfaces-0.10 \
	-lgstvideo-0.10

LIBS += \
	-framework Cocoa
	-framework QuickTime
	-framework OpenGL
