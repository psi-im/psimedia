plugin_LTLIBRARIES = libgstspeexdsp.la

libgstspeexdsp_la_SOURCES = speexdsp.c speexechoprobe.c
libgstspeexdsp_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS) $(SPEEXDSP_CFLAGS)
libgstspeexdsp_la_LIBADD = $(GST_LIBS) $(SPEEXDSP_LIBS)
libgstspeexdsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS)

noinst_HEADERS = speexdsp.h speexechoprobe.h
