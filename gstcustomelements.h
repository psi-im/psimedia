/*
 * Copyright (C) 2008  Barracuda Networks, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#ifndef GSTCUSTOMELEMENTS_H
#define GSTCUSTOMELEMENTS_H

#include <glib/gthread.h>
#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

G_BEGIN_DECLS

// We create three custom elements here
//
//   appvideosink - grab raw decoded frames, ready for painting
//   apprtpsrc    - allow the app to feed in RTP packets
//   apprtpsink   - allow the app to collect RTP packets

// set up the defines/typedefs

#define GST_TYPE_APPVIDEOSINK \
  (gst_appvideosink_get_type())
#define GST_APPVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_APPVIDEOSINK,GstAppVideoSink))
#define GST_APPVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_APPVIDEOSINK,GstAppVideoSinkClass))
#define GST_IS_APPVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_APPVIDEOSINK))
#define GST_IS_APPVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_APPVIDEOSINK))

typedef struct _GstAppVideoSink      GstAppVideoSink;
typedef struct _GstAppVideoSinkClass GstAppVideoSinkClass;

#define GST_TYPE_APPRTPSRC \
  (gst_apprtpsrc_get_type())
#define GST_APPRTPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_APPRTPSRC,GstAppRtpSrc))
#define GST_APPRTPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_APPRTPSRC,GstAppRtpSrcClass))
#define GST_IS_APPRTPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_APPRTPSRC))
#define GST_IS_APPRTPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_APPRTPSRC))

typedef struct _GstAppRtpSrc      GstAppRtpSrc;
typedef struct _GstAppRtpSrcClass GstAppRtpSrcClass;

#define GST_TYPE_APPRTPSINK \
  (gst_apprtpsink_get_type())
#define GST_APPRTPSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_APPRTPSINK,GstAppRtpSink))
#define GST_APPRTPSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_APPRTPSINK,GstAppRtpSinkClass))
#define GST_IS_APPRTPSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_APPRTPSINK))
#define GST_IS_APPRTPSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_APPRTPSINK))

typedef struct _GstAppRtpSink      GstAppRtpSink;
typedef struct _GstAppRtpSinkClass GstAppRtpSinkClass;

// done with defines/typedefs

// GstAppVideoSink

struct _GstAppVideoSink
{
	GstVideoSink parent;

	gpointer *appdata;
	void (*show_frame)(int width, int height, const unsigned char *rgb24, gpointer appdata);
};

struct _GstAppVideoSinkClass
{
	GstVideoSinkClass parent_class;
};

GType gst_appvideosink_get_type(void);

// GstAppRtpSrc

struct _GstAppRtpSrc
{
	GstPushSrc parent;

	GstBuffer *cur_buf;
	GMutex *push_mutex;
	GCond *push_cond;
	gboolean quit;

	GstCaps *caps;
};

struct _GstAppRtpSrcClass
{
	GstPushSrcClass parent_class;
};

GType gst_apprtpsrc_get_type(void);
void gst_apprtpsrc_packet_push(GstAppRtpSrc *src, const unsigned char *buf, int size);

// GstAppRtpSink

struct _GstAppRtpSink
{
	GstBaseSink parent;

	gpointer *appdata;
	void (*packet_ready)(const unsigned char *buf, int size, gpointer appdata);
};

struct _GstAppRtpSinkClass
{
	GstBaseSinkClass parent_class;
};

GType gst_apprtpsink_get_type(void);

G_END_DECLS

#endif
