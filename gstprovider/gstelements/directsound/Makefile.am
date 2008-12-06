plugin_LTLIBRARIES = libgstdirectsound.la

libgstdirectsound_la_SOURCES = \
	gstdirectsoundplugin.c \
	gstdirectsoundsink.c \
	gstdirectsoundsrc.c \
	gstdirectsound.c

libgstdirectsound_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(DIRECTSOUND_CFLAGS)

libgstdirectsound_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(DIRECTSOUND_LIBS) \
	-lgstinterfaces-$(GST_MAJORMINOR) \
	-lgstaudio-$(GST_MAJORMINOR)

libgstdirectsound_la_LDFLAGS = \
	$(GST_PLUGIN_LDFLAGS) \
	$(DIRECTSOUND_LDFLAGS)

libgstdirectsound_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstdirectsoundsink.h \
	gstdirectsoundsrc.h \
	gstdirectsound.h
