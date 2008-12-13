/*
 * Farsight Voice+Video library
 *
 *  Copyright 2008 Collabora Ltd
 *  Copyright 2008 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */



#ifndef __GST_SPEEX_ECHO_PROBE_H__
#define __GST_SPEEX_ECHO_PROBE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SPEEX_ECHO_PROBE            (gst_speex_echo_probe_get_type())
#define GST_SPEEX_ECHO_PROBE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPEEX_ECHO_PROBE,GstSpeexEchoProbe))
#define GST_IS_SPEEX_ECHO_PROBE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPEEX_ECHO_PROBE))
#define GST_SPEEX_ECHO_PROBE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_SPEEX_ECHO_PROBE,GstSpeexEchoProbeClass))
#define GST_IS_SPEEX_ECHO_PROBE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_SPEEX_ECHO_PROBE))
#define GST_SPEEX_ECHO_PROBE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_SPEEX_ECHO_PROBE,GstSpeexEchoProbeClass))

typedef struct _GstSpeexEchoProbe             GstSpeexEchoProbe;
typedef struct _GstSpeexEchoProbeClass        GstSpeexEchoProbeClass;

/**
 * GstSpeexEchoProbe:
 *
 * The adder object structure.
 */
struct _GstSpeexEchoProbe {
  GstElement    element;

  GstPad        *srcpad;
  GstPad        *sinkpad;

  /* Protected by the object lock */
  gint          rate;
  gint          channels;
  gboolean      channels_locked;

  /* Protected by the object lock */
  gint          latency;

  GstSegment    segment;

  /* Newer buffers at the head, protected by obj lock */
  GQueue        *buffers;
};

struct _GstSpeexEchoProbeClass {
  GstElementClass parent_class;
};

GType    gst_speex_echo_probe_get_type (void);

G_END_DECLS


#endif /* __GST_SPEEX_ECHO_PROBE_H__ */
