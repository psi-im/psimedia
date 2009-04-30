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

#include "speexdsp.h"

#include <stdio.h>
typedef struct
{
  FILE * fp;
  int offset;
} FileLog;

typedef struct
{
  char * pfname;
  char * cfname;
  FileLog * playback;
  FileLog * capture;
  GstClockTime start;
} PairLog;

GStaticMutex pairlog_mutex;
static PairLog * pairlog = NULL;

#include <string.h>
#include <gst/audio/audio.h>

extern GStaticMutex global_mutex;
extern GstSpeexDSP * global_dsp;
extern GstSpeexEchoProbe * global_probe;

GST_DEBUG_CATEGORY (speex_dsp_debug);
#define GST_CAT_DEFAULT (speex_dsp_debug)

#define DEFAULT_LATENCY_TUNE            (0)
#define DEFAULT_AGC                     (FALSE)
#define DEFAULT_AGC_INCREMENT           (12)
#define DEFAULT_AGC_DECREMENT           (-40)
#define DEFAULT_AGC_LEVEL               (8000)
#define DEFAULT_AGC_MAX_GAIN            (30)
#define DEFAULT_DENOISE                 (TRUE)
#define DEFAULT_ECHO_SUPPRESS           (-40)
#define DEFAULT_ECHO_SUPPRESS_ACTIVE    (-15)
#define DEFAULT_NOISE_SUPPRESS          (-15)

static const GstElementDetails gst_speex_dsp_details =
    GST_ELEMENT_DETAILS (
        "Voice processor",
        "Generic/Audio",
        "Preprepocesses voice with libspeexdsp",
        "Olivier Crete <olivier.crete@collabora.co.uk>");

static GstStaticPadTemplate gst_speex_dsp_rec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 6000, 48000 ], "
        "channels = (int) [1, MAX], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
        );

static GstStaticPadTemplate gst_speex_dsp_rec_src_template =
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
  PROP_PROBE,
  PROP_LATENCY_TUNE,
  PROP_AGC,
  PROP_AGC_INCREMENT,
  PROP_AGC_DECREMENT,
  PROP_AGC_LEVEL,
  PROP_AGC_MAX_GAIN,
  PROP_DENOISE,
  PROP_ECHO_SUPPRESS,
  PROP_ECHO_SUPPRESS_ACTIVE,
  PROP_NOISE_SUPPRESS
};

GST_BOILERPLATE(GstSpeexDSP, gst_speex_dsp, GstElement, GST_TYPE_ELEMENT);

static void
gst_speex_dsp_finalize (GObject * object);
static void
gst_speex_dsp_set_property (GObject * object,
    guint prop_id,
    const GValue * value,
    GParamSpec * pspec);
static void
gst_speex_dsp_get_property (GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec);

static GstStateChangeReturn
gst_speex_dsp_change_state (GstElement * element, GstStateChange transition);

static gboolean
gst_speex_dsp_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *
gst_speex_dsp_getcaps (GstPad * pad);

static GstFlowReturn
gst_speex_dsp_rec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean
gst_speex_dsp_rec_event (GstPad * pad, GstEvent * event);

static const GstQueryType *
gst_speex_dsp_query_type (GstPad * pad);
static gboolean
gst_speex_dsp_query (GstPad * pad, GstQuery * query);

static void
gst_speex_dsp_reset_locked (GstSpeexDSP * self);

static void
try_auto_attach ();

static GstBuffer *
try_echo_cancel (SpeexEchoState * echostate, const GstBuffer * recbuf,
    GstClockTime rec_adj, GstClockTime rec_base, GQueue * buffers, int rate,
    GstPad * srcpad, const GstCaps * outcaps, GstFlowReturn * res,
    GstSpeexDSP * obj);

static FileLog *
filelog_new (const char * fname)
{
  FileLog * fl;
  FILE *fp;

  fp = fopen (fname, "wb");
  if (!fp)
    return NULL;

  fl = (FileLog *)malloc (sizeof (FileLog));
  fl->fp = fp;
  fl->offset = 0;
  return fl;
}

static void
filelog_delete (FileLog * fl)
{
  fclose (fl->fp);
  free (fl);
}

static void
filelog_append (FileLog * fl, const unsigned char * buf, int offset, int size)
{
  int n, pad, start, len;

  pad = 0;
  start = 0;

  if (offset < fl->offset) {
    pad = 0;
    start = fl->offset - offset;
  }
  else if (offset > fl->offset) {
    pad = offset - fl->offset;
    start = 0;
  }

  len = size - start;
  if (len <= 0)
    return;

  for (n = 0; n < pad; ++n)
    fputc (0, fl->fp);

  if (fwrite (buf + start, len, 1, fl->fp) < 1)
    GST_DEBUG ("unable to write to log file");

  //fflush (fl->fp);
  fl->offset += pad + len;
}

/*static void
filelog_append_pad (FileLog * fl, int size)
{
  int n;
  for (n = 0; n < size; ++n)
    fputc (0, fl->fp);
}*/

static PairLog *
pairlog_new (const char * pfname, const char * cfname)
{
  PairLog * pl;
  pl = (PairLog *)malloc (sizeof (PairLog));
  pl->pfname = strdup (pfname);
  pl->cfname = strdup (cfname);
  pl->playback = NULL;
  pl->capture = NULL;
  pl->start = GST_CLOCK_TIME_NONE;
  return pl;
}

static void
pairlog_delete (PairLog * pl)
{
  if (pl->playback)
    filelog_delete (pl->playback);
  if (pl->capture)
    filelog_delete (pl->capture);
  free (pl->pfname);
  free (pl->cfname);
  free (pl);
}

static void
pairlog_append_playback (PairLog * pl, const unsigned char * buf, int offset, int size, GstClockTime time, int rate)
{
  gint64 i;

  if (rate <= 0) {
    GST_DEBUG ("bad rate");
    return;
  }

  if (!pl->playback) {
    pl->playback = filelog_new (pl->pfname);
    if (!pl->playback) {
      GST_DEBUG ("unable to create playback log '%s'", pl->pfname);
      return;
    }

    GST_DEBUG ("started playback log at %"GST_TIME_FORMAT,
        GST_TIME_ARGS (time));

    if (pl->capture)
      pl->start = time;
  }

  if (pl->start == GST_CLOCK_TIME_NONE)
    return;

  i = (((gint64)time - (gint64)pl->start) * rate / GST_SECOND) * 2;
  offset = (int)i;
  GST_LOG ("start=%"GST_TIME_FORMAT", time=%"GST_TIME_FORMAT", offset=%d",
      GST_TIME_ARGS (pl->start), GST_TIME_ARGS (time), offset);
  if (offset < 0)
    return;
  filelog_append (pl->playback, buf, offset, size);
}

static void
pairlog_append_capture (PairLog * pl, const unsigned char * buf, int offset, int size, GstClockTime time, int rate)
{
  gint64 i;

  if (rate <= 0) {
    GST_DEBUG ("bad rate");
    return;
  }

  if (!pl->capture) {
    pl->capture = filelog_new (pl->cfname);
    if (!pl->capture) {
      GST_DEBUG ("unable to create capture log '%s'", pl->cfname);
      return;
    }

    GST_DEBUG ("started capture log at %"GST_TIME_FORMAT,
        GST_TIME_ARGS (time));

    if (pl->playback)
      pl->start = time;
  }

  if (pl->start == GST_CLOCK_TIME_NONE)
    return;

  i = (((gint64)time - (gint64)pl->start) * rate / GST_SECOND) * 2;
  offset = (int)i;
  GST_LOG ("start=%"GST_TIME_FORMAT", time=%"GST_TIME_FORMAT", offset=%d",
      GST_TIME_ARGS (pl->start), GST_TIME_ARGS (time), offset);
  if (offset < 0)
    return;
  filelog_append (pl->capture, buf, offset, size);
}

static gboolean
have_env (const char *var)
{
  const char *val = g_getenv (var);
  if (val && strcmp (val, "1") == 0)
    return TRUE;
  else
    return FALSE;
}

// within a hundredth of a millisecond
static gboolean
near_enough_to (GstClockTime a, GstClockTime b)
{
  GstClockTime dist = GST_MSECOND / 100 / 2;
  if (b >= a - dist && b <= a + dist)
    return TRUE;
  else
    return FALSE;
}

static void
adapter_push_at (GstAdapter * adapter, GstBuffer * buffer, int offset)
{
  int size;
  int n, pad, start, len;
  int end;
  GstBuffer * newbuf;
  char * p;

  pad = 0;
  start = 0;
  size = GST_BUFFER_SIZE (buffer);
  end = gst_adapter_available (adapter);

  if (offset < end) {
    pad = 0;
    start = end - offset;
  }
  else if (offset > end) {
    pad = offset - end;
    start = 0;
  }

  len = size - start;
  if (len <= 0)
    return;

  newbuf = gst_buffer_new_and_alloc (pad + len);
  p = (char *)GST_BUFFER_DATA (newbuf);
  for (n = 0; n < pad; ++n)
    *(p++) = 0;

  memcpy (p, GST_BUFFER_DATA (buffer) + start, len);
  gst_adapter_push (adapter, newbuf);
  gst_buffer_unref (buffer);
}

// -----

static void
gst_speex_dsp_base_init (gpointer klass)
{
  GST_DEBUG_CATEGORY_INIT
      (speex_dsp_debug, "speexdsp", 0, "libspeexdsp wrapping elements");
}

static void
gst_speex_dsp_class_init (GstSpeexDSPClass * klass)
{
  GObjectClass * gobject_class;
  GstElementClass * gstelement_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_speex_dsp_finalize;
  gobject_class->set_property = gst_speex_dsp_set_property;
  gobject_class->get_property = gst_speex_dsp_get_property;

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_speex_dsp_rec_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_speex_dsp_rec_sink_template));
  gst_element_class_set_details (gstelement_class, &gst_speex_dsp_details);

  gstelement_class->change_state = gst_speex_dsp_change_state;

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (gobject_class,
      PROP_PROBE,
      g_param_spec_object ("probe",
          "A probe that gathers the buffers to do echo cancellation on",
          "This is a link to the probe that gets buffers to cancel the echo"
          " against",
          GST_TYPE_SPEEX_ECHO_PROBE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_LATENCY_TUNE,
      g_param_spec_int ("latency-tune",
          "Add/remove latency",
          "Use this to tune the latency value, in milliseconds, in case it is"
          " detected incorrectly",
          G_MININT, G_MAXINT, DEFAULT_LATENCY_TUNE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_AGC,
      g_param_spec_boolean ("agc",
          "Automatic Gain Control state",
          "Enable or disable automatic Gain Control state",
          DEFAULT_AGC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_AGC_INCREMENT,
      g_param_spec_int ("agc-increment",
          "Maximal gain increase in dB/second",
          "Maximal gain increase in dB/second",
          G_MININT, G_MAXINT, DEFAULT_AGC_INCREMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_AGC_DECREMENT,
      g_param_spec_int ("agc-decrement",
          "Maximal gain increase in dB/second",
          "Maximal gain increase in dB/second",
          G_MININT, G_MAXINT, DEFAULT_AGC_DECREMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_AGC_LEVEL,
      g_param_spec_float ("agc-level",
          "Automatic Gain Control level",
          "Automatic Gain Control level",
          -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_AGC_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_AGC_MAX_GAIN,
      g_param_spec_int ("agc-max-gain",
          "Maximal gain in dB",
          "Maximal gain in dB",
          G_MININT, G_MAXINT, DEFAULT_AGC_MAX_GAIN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DENOISE,
      g_param_spec_boolean ("denoise",
          "Denoiser state",
          "Enable or disable denoiser state",
          DEFAULT_DENOISE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ECHO_SUPPRESS,
      g_param_spec_int ("echo-suppress",
          "Maximum attenuation of the residual echo in dB",
          "Maximum attenuation of the residual echo in dB (negative number)",
          G_MININT, 0, DEFAULT_ECHO_SUPPRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ECHO_SUPPRESS_ACTIVE,
      g_param_spec_int ("echo-suppress-active",
          "Maximum attenuation of the residual echo in dB"
          " when near end is active",
          "Maximum attenuation of the residual echo in dB"
          " when near end is active (negative number)",
          G_MININT, 0, DEFAULT_ECHO_SUPPRESS_ACTIVE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_NOISE_SUPPRESS,
      g_param_spec_int ("noise-suppress",
          "Maximum attenuation of the noise in dB",
          "Maximum attenuation of the noise in dB (negative number)",
          G_MININT, 0, DEFAULT_NOISE_SUPPRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_speex_dsp_init (GstSpeexDSP * self, GstSpeexDSPClass *klass)
{
  GstPadTemplate * template;

  template = gst_static_pad_template_get (&gst_speex_dsp_rec_src_template);
  self->rec_srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);
  gst_pad_set_getcaps_function (self->rec_srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_getcaps));
  gst_pad_set_event_function (self->rec_srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_rec_event));
  gst_pad_set_query_function (self->rec_srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_query));
  gst_pad_set_query_type_function (self->rec_srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_query_type));
  gst_element_add_pad (GST_ELEMENT (self), self->rec_srcpad);

  template = gst_static_pad_template_get (&gst_speex_dsp_rec_sink_template);
  self->rec_sinkpad = gst_pad_new_from_template (template, "sink");
  gst_object_unref (template);
  gst_pad_set_chain_function (self->rec_sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_rec_chain));
  gst_pad_set_getcaps_function (self->rec_sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_getcaps));
  gst_pad_set_setcaps_function (self->rec_sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_setcaps));
  gst_pad_set_event_function (self->rec_sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_dsp_rec_event));
  gst_element_add_pad (GST_ELEMENT (self), self->rec_sinkpad);

  self->channels = 1;

  self->frame_size_ms = 20;
  self->filter_length_ms = 200;

  self->rec_adapter = gst_adapter_new ();
  self->rec_time = GST_CLOCK_TIME_NONE;
  self->rec_offset = GST_BUFFER_OFFSET_NONE;

  self->probe = NULL;
  self->latency_tune = DEFAULT_LATENCY_TUNE;
  self->agc = DEFAULT_AGC;
  self->agc_increment = DEFAULT_AGC_INCREMENT;
  self->agc_decrement = DEFAULT_AGC_DECREMENT;
  self->agc_level = DEFAULT_AGC_LEVEL;
  self->agc_max_gain = DEFAULT_AGC_MAX_GAIN;
  self->denoise = DEFAULT_DENOISE;
  self->echo_suppress = DEFAULT_ECHO_SUPPRESS;
  self->echo_suppress_active = DEFAULT_ECHO_SUPPRESS_ACTIVE;
  self->noise_suppress = DEFAULT_NOISE_SUPPRESS;
  self->buffers = g_queue_new();

  g_static_mutex_lock (&pairlog_mutex);
  if (!pairlog && have_env("SPEEXDSP_LOG"))
    pairlog = pairlog_new ("gst_play.raw", "gst_rec.raw");
  g_static_mutex_unlock (&pairlog_mutex);

  g_static_mutex_lock (&global_mutex);
  if (!global_dsp) {
    global_dsp = self;
    try_auto_attach ();
  }
  g_static_mutex_unlock (&global_mutex);
}

static void
gst_speex_dsp_finalize (GObject * object)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (object);

  g_static_mutex_lock (&global_mutex);
  if (global_dsp && global_dsp == self) {
    if (global_probe && global_probe == self->probe)
      GST_DEBUG_OBJECT (self, "speexdsp detaching from globally discovered speexechoprobe");

    global_dsp = NULL;
  }
  g_static_mutex_unlock (&global_mutex);

  if (self->probe) {
    GST_OBJECT_LOCK (self->probe);
    self->probe->dsp = NULL;
    GST_OBJECT_UNLOCK (self->probe);

    g_object_unref (self->probe);
    self->probe = NULL;
  }

  g_queue_foreach (self->buffers, (GFunc) gst_mini_object_unref, NULL);
  g_queue_free (self->buffers);

  if (self->preprocstate)
    speex_preprocess_state_destroy (self->preprocstate);
  if (self->echostate)
    speex_echo_state_destroy (self->echostate);

  g_object_unref (self->rec_adapter);

  g_static_mutex_lock (&pairlog_mutex);
  if (pairlog) {
    pairlog_delete (pairlog);
    pairlog = NULL;
  }
  g_static_mutex_unlock (&pairlog_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_speex_dsp_set_property (GObject * object,
    guint prop_id,
    const GValue * value,
    GParamSpec * pspec)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id)
  {
    case PROP_PROBE:
      if (G_LIKELY (g_value_get_object (value) != self->probe))
      {
        if (self->probe)
          gst_speex_dsp_detach (self);

        if (g_value_get_object (value))
          gst_speex_dsp_attach (self, g_value_get_object (value));
      }
      break;
    case PROP_LATENCY_TUNE:
      self->latency_tune = g_value_get_int (value);
      break;
    case PROP_AGC:
      self->agc = g_value_get_boolean (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_AGC,
            &self->agc);
      break;
    case PROP_AGC_INCREMENT:
      self->agc_increment = g_value_get_int (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_AGC_INCREMENT,
            &self->agc_increment);
      break;
    case PROP_AGC_DECREMENT:
      self->agc_decrement = g_value_get_int (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_AGC_DECREMENT,
            &self->agc_decrement);
      break;
    case PROP_AGC_LEVEL:
      self->agc_level = g_value_get_float (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_AGC_LEVEL,
            &self->agc_level);
      break;
    case PROP_AGC_MAX_GAIN:
      self->agc_max_gain = g_value_get_int (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_AGC_MAX_GAIN,
            &self->agc_max_gain);
      break;
    case PROP_DENOISE:
      self->denoise = g_value_get_boolean (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_DENOISE,
            &self->denoise);
      break;
    case PROP_ECHO_SUPPRESS:
      self->echo_suppress = g_value_get_int (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,
            &self->echo_suppress);
      break;
    case PROP_ECHO_SUPPRESS_ACTIVE:
      self->echo_suppress_active= g_value_get_int (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,
            &self->echo_suppress_active);
      break;
    case PROP_NOISE_SUPPRESS:
      self->noise_suppress = g_value_get_int (value);
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
            &self->noise_suppress);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_speex_dsp_get_property (GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id)
  {
    case PROP_PROBE:
      g_value_set_object (value, self->probe);
      break;
    case PROP_LATENCY_TUNE:
      g_value_set_int (value, self->latency_tune);
      break;
    case PROP_AGC:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_AGC,
            &self->agc);
      g_value_set_boolean (value, self->agc);
      break;
    case PROP_AGC_INCREMENT:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_AGC_INCREMENT,
            &self->agc_increment);
      g_value_set_int (value, self->agc_increment);
      break;
    case PROP_AGC_DECREMENT:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_AGC_DECREMENT,
            &self->agc_decrement);
      g_value_set_int (value, self->agc_decrement);
      break;
    case PROP_AGC_LEVEL:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_AGC_LEVEL,
            &self->agc_level);
      g_value_set_float (value, self->agc_level);
      break;
    case PROP_AGC_MAX_GAIN:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_AGC_MAX_GAIN,
            &self->agc_max_gain);
      g_value_set_int (value, self->agc_max_gain);
      break;
    case PROP_DENOISE:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_DENOISE,
            &self->denoise);
      g_value_set_boolean (value, self->denoise);
      break;
    case PROP_ECHO_SUPPRESS:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_ECHO_SUPPRESS,
            &self->echo_suppress);
      g_value_set_int (value, self->echo_suppress);
      break;
    case PROP_ECHO_SUPPRESS_ACTIVE:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_ECHO_SUPPRESS_ACTIVE,
            &self->echo_suppress_active);
      g_value_set_int (value, self->echo_suppress_active);
      break;
    case PROP_NOISE_SUPPRESS:
      if (self->preprocstate)
        speex_preprocess_ctl (self->preprocstate,
            SPEEX_PREPROCESS_GET_NOISE_SUPPRESS,
            &self->noise_suppress);
      g_value_set_int (value, self->noise_suppress);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

/* we can only accept caps that we and downstream can handle. */
static GstCaps *
gst_speex_dsp_getcaps (GstPad * pad)
{
  GstSpeexDSP * self;
  GstCaps * result, * peercaps, * tmpcaps;

  self = GST_SPEEX_DSP (gst_pad_get_parent (pad));

  result = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  if (self->echostate != NULL)
  {
    GST_OBJECT_LOCK (self);
    gst_caps_set_simple (result,
        "rate", G_TYPE_INT, self->rate,
        "channels", G_TYPE_INT, self->channels,
        NULL);
    GST_OBJECT_UNLOCK (self);
    goto out;
  }

  GST_OBJECT_LOCK (self);
  if (self->probe)
  {
    GST_OBJECT_LOCK (self->probe);
    if (self->probe->rate)
      gst_caps_set_simple (result, "rate", G_TYPE_INT, self->probe->rate, NULL);
    GST_OBJECT_UNLOCK (self->probe);
  }
  GST_OBJECT_UNLOCK (self);

  if (pad == self->rec_sinkpad) {
    peercaps = gst_pad_peer_get_caps (self->rec_srcpad);
    if (peercaps) {
      tmpcaps = result;
      result = gst_caps_intersect (result, peercaps);
      gst_caps_unref (tmpcaps);
      gst_caps_unref (peercaps);
    }
  }
  else if (pad == self->rec_srcpad) {
    peercaps = gst_pad_peer_get_caps (self->rec_sinkpad);
    if (peercaps) {
      tmpcaps = result;
      result = gst_caps_intersect (result, peercaps);
      gst_caps_unref (tmpcaps);
      gst_caps_unref (peercaps);
    }
  }

out:
  gst_object_unref (self);
  return result;
}

static gboolean
gst_speex_dsp_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSpeexDSP * self;
  GstStructure * structure;
  gint rate;
  gint channels = 1;
  gboolean ret = TRUE;

  self = GST_SPEEX_DSP (gst_pad_get_parent (pad));

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

  if (self->echostate) {
    if (self->rate != rate) {
      GST_WARNING_OBJECT (self, "Wrong rate, got %d, expected %d",
          rate, self->rate);
      ret = FALSE;
    }
    if (self->channels != channels) {
      GST_WARNING_OBJECT (self, "Wrong channel count, got %d, expected %d",
          channels, self->channels);
      ret = FALSE;
    }
    goto done;
  }

  if (self->probe) {
    GST_OBJECT_LOCK (self->probe);
    if (self->probe->rate) {
      if (self->probe->rate != rate) {
        GST_WARNING_OBJECT (self, "Wrong rate, probe has %d, we have %d",
            self->probe->rate, rate);
        ret = FALSE;
      }
      else {
        self->probe->rate = rate;
      }
    }
    GST_OBJECT_UNLOCK (self->probe);
    if (!ret)
      goto done;
  }

  self->rate = rate;

  /*if (self->probe)*/ {
    guint probe_channels = 1;
    guint frame_size, filter_length;

    frame_size = rate * self->frame_size_ms / 1000;
    filter_length = rate * self->filter_length_ms / 1000;

    if (self->probe) {
      GST_OBJECT_LOCK (self->probe);
      probe_channels = self->probe->channels;
      GST_OBJECT_UNLOCK (self->probe);
    }

    // FIXME: if this is -1, then probe caps aren't set yet.  there should
    //   be a better solution besides forcing this to 1
    if (probe_channels == -1)
      probe_channels = 1;

    if (self->channels == 1 && probe_channels == 1) {
      GST_DEBUG_OBJECT (self, "speex_echo_state_init (%d, %d)",
          frame_size, filter_length);
      self->echostate = speex_echo_state_init (frame_size, filter_length);
    }
    else {
      GST_DEBUG_OBJECT (self, "speex_echo_state_init_mc (%d, %d, %d, %d)",
          frame_size, filter_length, self->channels, probe_channels);
      self->echostate = speex_echo_state_init_mc (frame_size, filter_length,
          self->channels, probe_channels);
    }
  }

  self->preprocstate = speex_preprocess_state_init (
      rate * self->frame_size_ms / 1000,
      rate);

  if (self->echostate) {
    speex_echo_ctl (self->echostate,
        SPEEX_ECHO_SET_SAMPLING_RATE,
        &rate);

    speex_preprocess_ctl (self->preprocstate,
        SPEEX_PREPROCESS_SET_ECHO_STATE,
        self->echostate);
  }

  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_AGC,
      &self->agc);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_AGC_INCREMENT,
      &self->agc_increment);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_AGC_DECREMENT,
      &self->agc_decrement);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_AGC_LEVEL,
      &self->agc_level);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_AGC_MAX_GAIN,
      &self->agc_max_gain);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_DENOISE,
      &self->denoise);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,
      &self->echo_suppress);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,
      &self->echo_suppress_active);
  speex_preprocess_ctl (self->preprocstate,
      SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
      &self->noise_suppress);

done:
  GST_OBJECT_UNLOCK (self);
  gst_object_unref (self);
  return ret;
}

static gboolean
gst_speex_dsp_rec_event (GstPad * pad, GstEvent * event)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP: /* synchronized */
      gst_adapter_clear (self->rec_adapter);
      self->rec_offset = 0;
      self->rec_time = GST_CLOCK_TIME_NONE;
      gst_segment_init (&self->rec_segment, GST_FORMAT_UNDEFINED);
      g_queue_foreach (self->buffers, (GFunc) gst_mini_object_unref, NULL);
      g_queue_clear (self->buffers);
      GST_OBJECT_LOCK (self);
      gst_speex_dsp_reset_locked (self);
      GST_OBJECT_UNLOCK (self);
      break;
    case GST_EVENT_NEWSEGMENT: /* synchronized */
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
        gst_segment_set_newsegment_full (&self->rec_segment, update, rate,
            applied_rate, format, start, stop, position);
      }
    default:
      break;
  }

  if (pad == self->rec_sinkpad)
    res = gst_pad_push_event (self->rec_srcpad, event);
  else
    res = gst_pad_push_event (self->rec_sinkpad, event);

out:
  gst_object_unref (self);
  return res;
}

static const GstQueryType *
gst_speex_dsp_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return types;
}

static gboolean
gst_speex_dsp_query (GstPad * pad, GstQuery * query)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      guint64 latency;
      GstPad * peer;

      if ((peer = gst_pad_get_peer (self->rec_sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (self, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */
          latency = ((guint64)self->frame_size_ms) * 1000000; /* to nanos */

          GST_DEBUG_OBJECT (self, "Our latency: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (latency));

          min += latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += latency;

          GST_DEBUG_OBJECT (self, "Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (self);
  return res;
}

// TODO
static GstFlowReturn
gst_speex_dsp_rec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (gst_pad_get_parent (pad));
  GstFlowReturn res = GST_FLOW_OK;
  GstBuffer * recbuffer = NULL;
  gint sampsize, bufsize;
  GstClockTime duration;
  GstSpeexEchoProbe * probe = NULL;
  gint rate = 0;
  GstClockTime base_time;
  GstClockTime skew_fix;
  gint buffer_offset;

  base_time = gst_element_get_base_time (GST_ELEMENT_CAST (self));
  skew_fix = base_time; // FIXME FIXME FIXME FIXME!

  GST_OBJECT_LOCK (self);
  if (self->probe)
    probe = g_object_ref (self->probe);
  rate = self->rate;
  GST_OBJECT_UNLOCK (self);

  sampsize = rate * self->frame_size_ms / 1000;
  bufsize = 2 * sampsize;
  duration = self->frame_size_ms * GST_MSECOND;

  // FIXME to the max
  if (GST_BUFFER_IS_DISCONT (buffer)) {
    GST_LOG_OBJECT (self, "***discontinuous! starting over");

    // clear adapter, because otherwise new data will be considered relative
    //   to what's left in the adapter, which is of course wrong.  we need
    //   timestamps in the adapter or something..
    gst_adapter_clear (self->rec_adapter);

    // clear played buffers, in case the discontinuity is due to a clock
    //   change, which means existing buffers are timestamped wrong (there's
    //   probably a better way to handle this...)
    GST_OBJECT_LOCK (self);
    g_queue_foreach (self->buffers, (GFunc) gst_mini_object_unref, NULL);
    g_queue_clear (self->buffers);
    GST_OBJECT_UNLOCK (self);
  }

  if (gst_adapter_available (self->rec_adapter) == 0) {
    GST_LOG_OBJECT (self, "The adapter is empty, its a good time to reset the"
        " timestamp and offset");
    self->rec_time = GST_CLOCK_TIME_NONE;
    self->rec_offset = GST_BUFFER_OFFSET_NONE;
  }

  buffer_offset = 0;
  if (self->rec_time != GST_CLOCK_TIME_NONE) {
    GstClockTime a, b;
    gint64 i;
    a = self->rec_time + ((gst_adapter_available (self->rec_adapter) / 2) * GST_SECOND / rate);
    b = GST_BUFFER_TIMESTAMP (buffer);
    i = (((gint64)GST_BUFFER_TIMESTAMP (buffer) - (gint64)self->rec_time) * rate / GST_SECOND) * 2;
    buffer_offset = (gint)i;
    if (!near_enough_to (a, b)) {
      GST_LOG_OBJECT (self, "***continuous buffer with wrong timestamp"
          " (want=%"GST_TIME_FORMAT", got=%"GST_TIME_FORMAT"),"
          " compensating %d bytes",
          GST_TIME_ARGS (a), GST_TIME_ARGS (b), buffer_offset);
    }
  }

  if (self->rec_time == GST_CLOCK_TIME_NONE)
    self->rec_time = GST_BUFFER_TIMESTAMP (buffer);
  if (self->rec_offset == GST_BUFFER_OFFSET_NONE)
    self->rec_offset = GST_BUFFER_OFFSET (buffer);

  {
    GstClockTime rec_rt;
    rec_rt = gst_segment_to_running_time (&self->rec_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer));

    GST_LOG_OBJECT (self, "Captured buffer at %"GST_TIME_FORMAT
        " (len=%"GST_TIME_FORMAT", offset=%lld, base=%lld)",
        //GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer) /*+ base_time*/),
        GST_TIME_ARGS (rec_rt),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
        GST_BUFFER_OFFSET (buffer), base_time);
  }

  g_static_mutex_lock (&pairlog_mutex);
  if (pairlog) {
    pairlog_append_capture (pairlog,
        (const unsigned char *)GST_BUFFER_DATA (buffer),
        GST_BUFFER_OFFSET (buffer) * 2,
        GST_BUFFER_SIZE (buffer),
        GST_BUFFER_TIMESTAMP (buffer) + skew_fix,
        rate);
  }
  g_static_mutex_unlock (&pairlog_mutex);

  // TODO: handle gaps (see above about discontinuity)
  //gst_adapter_push (self->rec_adapter, buffer);
  adapter_push_at (self->rec_adapter, buffer, buffer_offset);
  //res = gst_pad_push (self->rec_srcpad, buffer);
  //goto out;

  while (TRUE) {
    GstBuffer * outbuffer = NULL;
    //GstClockTime rec_rt = 0;

    // buffer at least 500ms + 1 frame before processing
    //if (gst_adapter_available (self->rec_adapter) < (2 * rate * 2000 / 1000) + bufsize)
    //  break;

    recbuffer = gst_adapter_take_buffer (self->rec_adapter, bufsize);
    if (!recbuffer)
      break;

    GST_BUFFER_TIMESTAMP (recbuffer) = self->rec_time;
    GST_BUFFER_OFFSET (recbuffer) = self->rec_offset;
    GST_BUFFER_DURATION (recbuffer) = duration;

    // FIXME: don't need this?
    //rec_rt = gst_segment_to_running_time (&self->rec_segment, GST_FORMAT_TIME,
    //    self->rec_time);

    GST_OBJECT_LOCK (self);
    outbuffer = try_echo_cancel (
        self->echostate,
        recbuffer,
        self->rec_time + skew_fix /*- (((GstClockTime)self->latency_tune) * GST_MSECOND)*/,
        base_time,
        self->buffers,
        rate,
        self->rec_srcpad,
        GST_PAD_CAPS (self->rec_sinkpad),
        &res,
        self);
    GST_OBJECT_UNLOCK (self);
    if (outbuffer) {
      /* if cancel succeeds, then post-processing occurs on the newly returned
       * buffer and we can free the original one.  newly returned buffer has
       * appropriate caps.
       */
      gst_buffer_unref (recbuffer);
    }
    else {
      /* if cancel fails, it's possible it was due to a flow error when
       * creating a new buffer */
      if (res != GST_FLOW_OK)
        goto out;

      /* if cancel fails, then post-processing occurs on the original buffer,
       * just make it writable and set appropriate caps.
       */
      outbuffer = gst_buffer_make_writable (recbuffer);
      gst_buffer_set_caps (outbuffer, GST_PAD_CAPS (self->rec_sinkpad));
    }

    GST_OBJECT_LOCK (self);
    speex_preprocess_run (self->preprocstate,
        (spx_int16_t *) GST_BUFFER_DATA (outbuffer));
    GST_OBJECT_UNLOCK (self);

    self->rec_time += duration;
    self->rec_offset += sampsize; // FIXME: does this work for >1 channels?

    GST_LOG_OBJECT (self, "Sending out buffer %p", outbuffer);
    res = gst_pad_push (self->rec_srcpad, outbuffer);
    if (res != GST_FLOW_OK)
      break;
  }

out:
  if (probe)
    gst_object_unref (probe);
  gst_object_unref (self);
  return res;
}

static void
gst_speex_dsp_reset_locked (GstSpeexDSP * self)
{
  if (self->preprocstate)
    speex_preprocess_state_destroy (self->preprocstate);
  self->preprocstate = NULL;
  if (self->echostate)
    speex_echo_state_destroy (self->echostate);
  self->echostate = NULL;
  self->rate = 0;
}

static GstStateChangeReturn
gst_speex_dsp_change_state (GstElement * element, GstStateChange transition)
{
  GstSpeexDSP * self;
  GstStateChangeReturn ret;

  self = GST_SPEEX_DSP (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (self);
      gst_speex_dsp_reset_locked (self);
      GST_OBJECT_UNLOCK (self);
      gst_segment_init (&self->rec_segment, GST_FORMAT_UNDEFINED);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  return ret;
}

/* lock global_mutex during this call */
static void
try_auto_attach ()
{
  if (global_probe) {
    gst_speex_dsp_attach (global_dsp, global_probe);
    GST_DEBUG_OBJECT (global_dsp, "speexdsp attaching to globally discovered speexechoprobe");
  }
}

void
gst_speex_dsp_set_auto_attach (GstSpeexDSP * self, gboolean enabled)
{
  g_static_mutex_lock (&global_mutex);
  if (enabled) {
    if (!global_dsp) {
      global_dsp = self;
      try_auto_attach ();
    }
  }
  else {
    if (global_dsp == self)
      global_dsp = NULL;
  }
  g_static_mutex_unlock (&global_mutex);
}

void
gst_speex_dsp_add_capture_buffer (GstSpeexDSP * self, GstBuffer * buf)
{
  GstClockTime base_time = gst_element_get_base_time (GST_ELEMENT_CAST (self));
  GstClockTime duration = GST_CLOCK_TIME_NONE;
  GstCaps * caps;
  GstStructure * structure;
  int rate = 0;

  GST_OBJECT_LOCK (self);
  if (self->rate != 0) {
    rate = self->rate;
    GST_OBJECT_UNLOCK (self);
  }
  else {
    GST_OBJECT_UNLOCK (self);
    caps = GST_BUFFER_CAPS (buf);
    if (caps) {
      structure = gst_caps_get_structure (GST_BUFFER_CAPS (buf), 0);
      if (structure)
        gst_structure_get_int (structure, "rate", &rate);
    }
  }

  if (rate != 0)
    duration = GST_BUFFER_SIZE (buf) * GST_SECOND / (rate * 2);

  GST_LOG_OBJECT (self, "Played buffer at %"GST_TIME_FORMAT
      " (len=%"GST_TIME_FORMAT", offset=%lld)",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf) - base_time),
      GST_TIME_ARGS (duration),
      GST_BUFFER_OFFSET (buf));

  g_static_mutex_lock (&pairlog_mutex);
  if (pairlog && rate != 0) {
    pairlog_append_playback (pairlog,
        (const unsigned char *)GST_BUFFER_DATA (buf),
        GST_BUFFER_OFFSET (buf) * 2,
        GST_BUFFER_SIZE (buf),
        GST_BUFFER_TIMESTAMP (buf) - base_time,
        rate);
  }
  g_static_mutex_unlock (&pairlog_mutex);

  GST_OBJECT_LOCK (self);
  g_queue_push_head (self->buffers, buf);
  GST_OBJECT_UNLOCK (self);
}

/* global_mutex locked during this call */
void
gst_speex_dsp_attach (GstSpeexDSP * self, GstSpeexEchoProbe * probe)
{
  g_object_ref (probe);
  self->probe = probe;
  GST_OBJECT_LOCK (probe);
  probe->dsp = self;
  GST_OBJECT_UNLOCK (probe);
}

/* global_mutex locked during this call */
void
gst_speex_dsp_detach (GstSpeexDSP * self)
{
  if (self->probe) {
    GST_OBJECT_LOCK (self->probe);
    self->probe->dsp = NULL;
    GST_OBJECT_UNLOCK (self->probe);
    g_object_unref (self->probe);
    self->probe = NULL;
  }
}

/* this function attempts to cancel echo.
 *
 * echostate: AEC state
 * recbuf:    recorded buffer to strip echo from
 * rec_adj:   time of recorded buffer with latency/skew adjustment
 * rec_base:  base time of element that received the recorded buffer
 * buffers:   a queue of recently played buffers, in clock time
 * rate:      sample rate being used (both rec/play are the same rate)
 * srcpad:    the pad that the resulting data should go out on
 * outcaps:   the caps of the resulting data
 * res:       if this function returns null, a flow value may be stored here
 * obj:       object to log debug messages against
 *
 * returns:   new buffer with echo cancelled, or NULL if cancelling was not
 *            possible.  check 'res' to see if the reason was a flow problem.
 *
 * note that while this function taks a pad as an argument, it does not
 *   actually send a buffer out on the pad.  it uses gst_pad_alloc_buffer()
 *   to create the output buffer, which requires a pad as input.
 */
GstBuffer *
try_echo_cancel (SpeexEchoState * echostate, const GstBuffer * recbuf,
    GstClockTime rec_adj, GstClockTime rec_base, GQueue * buffers, int rate,
    GstPad * srcpad, const GstCaps * outcaps, GstFlowReturn * res,
    GstSpeexDSP * obj)
{
  GstFlowReturn res_ = GST_FLOW_OK;
  GstBuffer * recbuffer = NULL;
  GstBuffer * play_buffer = NULL;
  gchar * buf = NULL;
  gint bufsize;
  GstClockTime duration;
  GstClockTime play_rt = 0, rec_rt = 0, rec_end = 0;
  gint rec_offset;
  GstBuffer * outbuffer = NULL;

  recbuffer = (GstBuffer *)recbuf;
  rec_rt = rec_adj;
  rec_offset = GST_BUFFER_OFFSET (recbuffer);
  bufsize = GST_BUFFER_SIZE (recbuffer);
  duration = GST_BUFFER_DURATION (recbuffer);

  if (!echostate) {
    GST_LOG_OBJECT (obj, "No echostate, not doing echo cancellation");
    return NULL;
  }

  /* clean out the queue, throwing out any buffers that are too old */
  while (TRUE) {
    play_buffer = g_queue_peek_tail (buffers);
    if (!play_buffer || GST_BUFFER_TIMESTAMP (play_buffer) - rec_base + (GST_BUFFER_SIZE (play_buffer) * GST_SECOND / (rate * 2)) >= rec_rt)
      break;
    GST_LOG_OBJECT (obj, "Throwing out old played buffer");
    g_queue_pop_tail (buffers);
    gst_buffer_unref (play_buffer);
  }

  play_buffer = g_queue_peek_tail (buffers);
  if (!play_buffer) {
    GST_LOG_OBJECT (obj, "No playout buffer, not doing echo cancellation");
    return NULL;
  }

  play_rt = GST_BUFFER_TIMESTAMP (play_buffer) - rec_base;

  GST_LOG_OBJECT (obj, "rec_start=%"GST_TIME_FORMAT","
      " play_start=%"GST_TIME_FORMAT"",
      GST_TIME_ARGS (rec_rt), GST_TIME_ARGS (play_rt));

  if (play_rt > rec_rt + duration) {
    GST_LOG_OBJECT (obj, "Have no buffers to compare, not cancelling echo");
    return NULL;
  }

  res_ = gst_pad_alloc_buffer (srcpad, rec_offset, bufsize,
      (GstCaps *)outcaps, &outbuffer);
  if (res_ != GST_FLOW_OK) {
    *res = res_;
    return NULL;
  }

  g_assert (outbuffer);

  // FIXME: what if GST_BUFFER_SIZE (outbuffer) != bufsize ?

  GST_BUFFER_TIMESTAMP (outbuffer) = GST_BUFFER_TIMESTAMP (recbuffer);
  GST_BUFFER_OFFSET (outbuffer) =  GST_BUFFER_OFFSET (recbuffer);
  GST_BUFFER_DURATION (outbuffer) = GST_BUFFER_DURATION (recbuffer);

  /* here's a buffer we'll fill up with played data.  we initialize it to
   * silence in case we don't have enough played data to populate the whole
   * thing.
   */
  buf = g_malloc0 (bufsize);

  /* canceling is done relative to rec_rt, even though it may not be the same
   * as the actual timestamp of the recorded buffer (rec_rt has latency_tune
   * applied)
   */
  rec_end = rec_rt + duration;
  rec_offset = 0; // FIXME: slightly confusing, reusing this variable for
      // different purpose

  while (rec_offset < bufsize) {
    GstClockTime play_duration, time;
    gint play_offset, size;

    play_buffer = g_queue_peek_tail (buffers);
    if (!play_buffer) {
      GST_LOG_OBJECT (obj, "Queue empty, can't cancel everything");
      break;
    }

    play_rt = GST_BUFFER_TIMESTAMP (play_buffer) - rec_base;

    if (rec_end < play_rt) {
      GST_LOG_OBJECT (obj, "End of recorded buffer (at %"GST_TIME_FORMAT")"
          " is before any played buffer"
          " (which start at %"GST_TIME_FORMAT")",
          GST_TIME_ARGS (rec_end),
          GST_TIME_ARGS (play_rt));
      break;
    }

    play_duration = GST_BUFFER_SIZE (play_buffer) * GST_SECOND /
        (rate * 2);

    // FIXME: it seems we already do something like this earlier.  we
    //   shouldn't need it in two spots, and the one here is probably
    //   more appropriate
    if (play_rt + play_duration < rec_rt) {
      GST_LOG_OBJECT (obj, "Start of rec data (at %"GST_TIME_FORMAT")"
          " after the end of played data (at %"GST_TIME_FORMAT")",
          GST_TIME_ARGS (rec_rt),
          GST_TIME_ARGS (play_rt + play_duration));
      g_queue_pop_tail (buffers);
      gst_buffer_unref (play_buffer);
      continue;
    }

    if (rec_rt > play_rt) {
      GstClockTime time_diff = rec_rt - play_rt;
      time_diff *= 2 * rate;
      time_diff /= GST_SECOND;
      time_diff &= ~0x01; // ensure even

      play_offset = time_diff;
      GST_LOG_OBJECT (obj, "rec>play off: %d", play_offset);
    }
    else {
      gint rec_skip;
      GstClockTime time_diff = play_rt - rec_rt;
      time_diff *= 2 * rate;
      time_diff /= GST_SECOND;
      time_diff &= ~0x01; // ensure even

      rec_skip = time_diff;
      rec_rt += rec_skip * GST_SECOND / (rate * 2);
      rec_offset += rec_skip;
      play_offset = 0;
      GST_LOG_OBJECT (obj, "rec<=play off: %d", rec_skip);
    }

    play_offset = MIN (play_offset, GST_BUFFER_SIZE (play_buffer));
    size = MIN (GST_BUFFER_SIZE (play_buffer) - play_offset,
        bufsize - rec_offset);

    time = play_offset;
    time *= GST_SECOND;
    time /= rate * 2;
    time += play_rt;

    GST_LOG_OBJECT (obj, "Cancelling data recorded at %"GST_TIME_FORMAT
        " with data played at %"GST_TIME_FORMAT
        " (difference %"GST_TIME_FORMAT") for %d bytes",
        GST_TIME_ARGS (rec_rt),
        GST_TIME_ARGS (time),
        GST_TIME_ARGS (rec_rt - time),
        size);

    GST_LOG_OBJECT (obj, "using play buffer %p (size=%d), mid(%d, %d)",
        play_buffer, GST_BUFFER_SIZE (play_buffer), play_offset, size);

    if(rec_offset < 0 || play_offset < 0 || rec_offset + size > bufsize || play_offset + size > GST_BUFFER_SIZE (play_buffer))
    {
      fprintf (stderr, "***speexdsp explosions!\n");
      abort();
      return NULL;
    }

    memcpy (buf + rec_offset, GST_BUFFER_DATA (play_buffer) + play_offset,
        size);

    rec_rt += size * GST_SECOND / (rate * 2);
    rec_offset += size;

    if (GST_BUFFER_SIZE (play_buffer) == play_offset + size) {
      GstBuffer *pb = g_queue_pop_tail (buffers);
      gst_buffer_unref (play_buffer);
      g_assert (pb == play_buffer);
    }
  }

  GST_LOG_OBJECT (obj, "Cancelling echo");

  speex_echo_cancellation (echostate,
      (const spx_int16_t *) GST_BUFFER_DATA (recbuffer),
      (const spx_int16_t *) buf,
      (spx_int16_t *) GST_BUFFER_DATA (outbuffer));

  g_free (buf);

  return outbuffer;
}
