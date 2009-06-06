plugin_LTLIBRARIES = libgstdirectsound.la

libgstdirectsound_la_SOURCES =  gstdirectsound.c \
	gstdirectsoundringbuffer.c gstdirectsoundsink.c \
	gstdirectsoundsrc.c gstdirectsoundplugin.c
libgstdirectsound_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(DIRECTSOUND_CFLAGS)
libgstdirectsound_la_LIBADD = $(DIRECTSOUND_LIBS) \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR)
libgstdirectsound_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)

noinst_HEADERS = gstdirectsound.h gstdirectsoundringbuffer.h \
	gstdirectsoundsink.h gstdirectsoundsrc.h
