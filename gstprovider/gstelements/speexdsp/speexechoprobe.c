/*
 * Farsight Voice+Video library
 *
 *  Copyright 2008 Collabora Ltd
 *  Copyright 2008 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "speexechoprobe.h"
#include "speexdsp.h"

#include <gst/audio/audio.h>

#include <string.h>

#define GST_CAT_DEFAULT (speex_dsp_debug)

/* elementfactory information */
static const GstElementDetails gst_speex_echo_probe_details =
GST_ELEMENT_DETAILS (
  "Accoustic Echo canceller probe",
  "Generic/Audio",
  "Gathers playback buffers for speexdsp",
  "Olivier Crete <olivier.crete@collabora.co.uk>");


static GstStaticPadTemplate gst_speex_echo_probe_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 6000, 48000 ], "
        "channels = (int) [1, MAX], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
        );

static GstStaticPadTemplate gst_speex_echo_probe_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 6000, 48000 ], "
        "channels = (int) [1, MAX], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
        );

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

GST_BOILERPLATE(GstSpeexEchoProbe, gst_speex_echo_probe, GstElement,
    GST_TYPE_ELEMENT);


static void
gst_speex_echo_probe_finalize (GObject * object);


static GstStateChangeReturn
gst_speex_echo_probe_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn
gst_speex_echo_probe_chain (GstPad *pad, GstBuffer *buffer);
static gboolean
gst_speex_echo_probe_event (GstPad *pad, GstEvent *event);
static gboolean
gst_speex_echo_probe_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *
gst_speex_echo_probe_getcaps (GstPad * pad);


static void
gst_speex_echo_probe_base_init (gpointer klass)
{
}

static void
gst_speex_echo_probe_class_init (GstSpeexEchoProbeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_speex_echo_probe_finalize;

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_speex_echo_probe_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_speex_echo_probe_sink_template));
  gst_element_class_set_details (gstelement_class,
      &gst_speex_echo_probe_details);

  gstelement_class->change_state = gst_speex_echo_probe_change_state;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_speex_echo_probe_init (GstSpeexEchoProbe * self, GstSpeexEchoProbeClass *klass)
{
  GstPadTemplate *template;

  template = gst_static_pad_template_get (&gst_speex_echo_probe_src_template);
  self->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_echo_probe_event));
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_echo_probe_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  template = gst_static_pad_template_get (&gst_speex_echo_probe_sink_template);
  self->sinkpad = gst_pad_new_from_template (template, "ink");
  gst_object_unref (template);
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_echo_probe_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_echo_probe_event));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_echo_probe_setcaps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_echo_probe_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->buffers = g_queue_new ();

  self->latency = -1;

  self->channels = 1;
}

static void
gst_speex_echo_probe_finalize (GObject * object)
{
  GstSpeexEchoProbe *self = GST_SPEEX_ECHO_PROBE (object);

  g_queue_free (self->buffers);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* the first caps we receive on any of the sinkpads will define the caps for all
 * the other sinkpads because we can only mix streams with the same caps.
 * */
static gboolean
gst_speex_echo_probe_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSpeexEchoProbe *self = GST_SPEEX_ECHO_PROBE (gst_pad_get_parent (pad));
  gint rate, channels = 1;
  GstStructure *structure;
  gboolean ret = TRUE;

  GST_LOG_OBJECT (self, "setting caps on pad %p,%s to %" GST_PTR_FORMAT, pad,
      GST_PAD_NAME (pad), caps);

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate)) {
    GST_WARNING_OBJECT (self, "Tried to set caps without a rate");
    gst_object_unref (self);
    return FALSE;
  }

  gst_structure_get_int (structure, "channels", &channels);

  GST_OBJECT_LOCK (self);

  if (self->rate && self->rate != rate) {
    GST_WARNING_OBJECT (self, "Wrong rate, got %d, expected %d",
        rate, self->rate);
    ret = FALSE;
  }
  else {
  }

  if (self->channels_locked && self->channels != channels) {
    GST_WARNING_OBJECT (self, "Can no change the number of channels while"
        "the main speexdsp is running");
    ret = FALSE;
  }

  if (ret) {
    self->rate = rate;
    self->channels = channels;
  }

  GST_OBJECT_UNLOCK (self);

  gst_object_unref (self);

  return ret;
}

static GstCaps *
gst_speex_echo_probe_getcaps (GstPad * pad)
{
  GstSpeexEchoProbe *self;
  GstCaps *result, *peercaps, *tmpcaps;

  self = GST_SPEEX_ECHO_PROBE (gst_pad_get_parent (pad));

  result = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  GST_OBJECT_LOCK (self);
  if (self->rate)
    gst_caps_set_simple (result, "rate", G_TYPE_INT, self->rate, NULL);

  if (self->channels_locked)
    gst_caps_set_simple (result, "channels", G_TYPE_INT, self->channels, NULL);
  GST_OBJECT_UNLOCK (self);

  if (pad == self->sinkpad) {
    peercaps = gst_pad_peer_get_caps (self->srcpad);
    if (peercaps) {
      tmpcaps = result;
      result = gst_caps_intersect (result, peercaps);
      gst_caps_unref (tmpcaps);
      gst_caps_unref (peercaps);
    }
  }
  else if (pad == self->srcpad) {
    peercaps = gst_pad_peer_get_caps (self->sinkpad);
    if (peercaps) {
      tmpcaps = result;
      result = gst_caps_intersect (result, peercaps);
      gst_caps_unref (tmpcaps);
      gst_caps_unref (peercaps);
    }
  }

  gst_object_unref (self);

  return result;
}

static gboolean
gst_speex_echo_probe_event (GstPad *pad, GstEvent *event)
{
  GstSpeexEchoProbe *self = GST_SPEEX_ECHO_PROBE (gst_pad_get_parent (pad));
  gboolean res = FALSE;
  GstClockTime latency;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_LATENCY:
      gst_event_parse_latency (event, &latency);

      GST_OBJECT_LOCK (self);
      self->latency = latency;
      GST_OBJECT_UNLOCK (self);

      GST_DEBUG_OBJECT (self, "We have a latency of %"GST_TIME_FORMAT,
          GST_TIME_ARGS (latency));
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_OBJECT_LOCK (self);
      g_queue_foreach (self->buffers, (GFunc) gst_mini_object_unref, NULL);
      g_queue_clear (self->buffers);
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      self->rate = 0;
      self->channels = 1;
      GST_OBJECT_UNLOCK (self);
      break;
    case GST_EVENT_NEWSEGMENT:
      {
        gboolean update;
        gdouble rate;
        gdouble applied_rate;
        GstFormat format;
        gint64 start;
        gint64 stop;
        gint64 position;

        gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
            &format, &start, &stop, &position);

        if (rate != 1.0 || applied_rate != 1.0)
        {
          GST_ERROR_OBJECT (self, "Only a rate of 1.0 is allowed");
          goto out;
        }

        if (format != GST_FORMAT_TIME)
        {
          GST_ERROR_OBJECT (self, "Only times segments are allowed");
          goto out;
        }

        GST_OBJECT_LOCK (self);
        gst_segment_set_newsegment_full (&self->segment, update, rate,
            applied_rate, format, start, stop, position);
        GST_OBJECT_UNLOCK (self);
      }
      break;
    default:
      break;
  }

  if (pad == self->sinkpad)
    res = gst_pad_push_event (self->srcpad, event);
  else
    res = gst_pad_push_event (self->sinkpad, event);

 out:

  gst_object_unref (self);

  return res;
}



static GstFlowReturn
gst_speex_echo_probe_chain (GstPad *pad, GstBuffer *buffer)
{
  GstSpeexEchoProbe *self = GST_SPEEX_ECHO_PROBE (gst_pad_get_parent (pad));
  GstFlowReturn res;
  GstBuffer *newbuf = NULL;

  GST_OBJECT_LOCK (self);
  gst_buffer_ref (buffer);
  newbuf = gst_buffer_create_sub (buffer, 0, GST_BUFFER_SIZE (buffer));
  /* Moves the buffer timestamp to be in Running time */
  GST_BUFFER_TIMESTAMP (newbuf) =
      gst_segment_to_running_time (&self->segment,
          GST_FORMAT_TIME,
          GST_BUFFER_TIMESTAMP (newbuf));
  g_queue_push_head (self->buffers, newbuf);
  GST_OBJECT_UNLOCK (self);

  res = gst_pad_push (self->srcpad, buffer);

  gst_object_unref (self);

  return res;
}


static GstStateChangeReturn
gst_speex_echo_probe_change_state (GstElement * element,
    GstStateChange transition)
{
  GstSpeexEchoProbe *self;
  GstStateChangeReturn ret;

  self = GST_SPEEX_ECHO_PROBE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (self);
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      self->rate = 0;
      self->channels = 1;
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}
