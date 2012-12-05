/*
 * Farsight Voice+Video library
 *
 *  Copyright 2008 Collabora Ltd
 *  Copyright 2008 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  Copyright 2009 Barracuda Networks, Inc
 *   @author: Justin Karneges <justin@affinix.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef __GST_SPEEX_DSP_H__
#define __GST_SPEEX_DSP_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include "speexechoprobe.h"

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (speex_dsp_debug);

#define GST_TYPE_SPEEX_DSP \
  (gst_speex_dsp_get_type())
#define GST_SPEEX_DSP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPEEX_DSP,GstSpeexDSP))
#define GST_IS_SPEEX_DSP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPEEX_DSP))
#define GST_SPEEX_DSP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPEEX_DSP,GstSpeexDSPClass))
#define GST_IS_SPEEX_DSP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPEEX_DSP))
#define GST_SPEEX_DSP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_SPEEX_DSP,GstSpeexDSPClass))

typedef struct _GstSpeexDSP GstSpeexDSP;
typedef struct _GstSpeexDSPClass GstSpeexDSPClass;

struct _GstSpeexDSP
{
  GstElement element;

  GstPad * rec_srcpad;
  GstPad * rec_sinkpad;

  /* Protected by the stream lock */
  guint frame_size_ms; /* frame size in ms */
  guint filter_length_ms; /* filter length in ms */

  /* Protected by the object lock */
  gint rate;
  gint channels;

  /* Protected by the stream lock */
  GstSegment rec_segment;

  GstAdapter * rec_adapter;

  GstClockTime rec_time;
  guint64 rec_offset;

  /* Protected by the object lock */
  SpeexPreprocessState * preprocstate;

  /* Protected by the stream lock */
  SpeexEchoState * echostate;

  /* Protected by the object lock */
  GstSpeexEchoProbe * probe;
  GQueue * buffers;

  /* Protected by the object lock */
  gint latency_tune;
  gboolean agc;
  gint agc_increment;
  gint agc_decrement;
  gfloat agc_level;
  gint agc_max_gain;
  gboolean denoise;
  gint echo_suppress;
  gint echo_suppress_active;
  gint noise_suppress;
};

struct _GstSpeexDSPClass
{
  GstElementClass parent_class;
};

GType gst_speex_dsp_get_type (void);

void gst_speex_dsp_set_auto_attach (GstSpeexDSP * self, gboolean enabled);
void gst_speex_dsp_add_capture_buffer (GstSpeexDSP * self, GstBuffer * buf);

/* called by probe, with global_mutex locked */
void gst_speex_dsp_attach (GstSpeexDSP * self, GstSpeexEchoProbe * probe);
void gst_speex_dsp_detach (GstSpeexDSP * self);

G_END_DECLS

#endif /* __GST_SPEEX_DSP_H__ */
