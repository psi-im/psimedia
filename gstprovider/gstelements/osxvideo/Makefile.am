# FIXME: clean up this crap
OBJC=gcc

plugin_LTLIBRARIES = libgstosxvideosink.la

libgstosxvideosink_la_SOURCES = osxvideoplugin.m \
	osxvideosink.m \
	cocoawindow.m \
	osxvideosrc.c
libgstosxvideosink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) -Wno-deprecated-declarations # Apple is hiring monkeys nowadays
libgstosxvideosink_la_LIBADD =  \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR)

libgstosxvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
	-Wl,-framework -Wl,Cocoa -Wl,-framework -Wl,QuickTime \
	-Wl,-framework -Wl,OpenGL
libgstosxvideosink_la_LIBTOOLFLAGS = --tag=disable-static

AM_OBJCFLAGS=$(CFLAGS) $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)

noinst_HEADERS = osxvideosink.h osxvideosrc.h cocoawindow.h
