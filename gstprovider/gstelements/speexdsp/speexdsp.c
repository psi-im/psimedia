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

#include <string.h>
#include <gst/audio/audio.h>

extern GStaticMutex global_mutex;
extern GObject * global_dsp;
extern GObject * global_probe;

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

static void
gst_speex_dsp_reset_locked (GstSpeexDSP * self);

static void
try_auto_attach ();

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

  g_static_mutex_lock (&global_mutex);
  if (!global_dsp) {
    global_dsp = G_OBJECT (self);
    try_auto_attach ();
  }
  g_static_mutex_unlock (&global_mutex);
}

static void
gst_speex_dsp_finalize (GObject * object)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (object);

  if (self->probe) {
    gst_speex_echo_probe_capture_stop (self->probe);

    g_static_mutex_lock (&global_mutex);
    if (global_dsp && global_dsp == G_OBJECT (self) && global_probe && G_OBJECT (self->probe) == global_probe) {
      GST_DEBUG_OBJECT (self, "speexdsp detaching from globally discovered speexechoprobe");
      global_dsp = NULL;
    }
    g_static_mutex_unlock (&global_mutex);

    g_object_unref (self->probe);
    self->probe = NULL;
  }

  if (self->preprocstate)
    speex_preprocess_state_destroy (self->preprocstate);
  if (self->echostate)
    speex_echo_state_destroy (self->echostate);

  g_object_unref (self->rec_adapter);

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

  if (self->probe) {
    guint probe_channels = 1;
    guint frame_size, filter_length;

    frame_size = rate * self->frame_size_ms / 1000;
    filter_length = rate * self->filter_length_ms / 1000;

    GST_OBJECT_LOCK (self->probe);
    probe_channels = self->probe->channels;
    GST_OBJECT_UNLOCK (self->probe);

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

// TODO
static GstFlowReturn
gst_speex_dsp_rec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSpeexDSP * self = GST_SPEEX_DSP (gst_pad_get_parent (pad));
  GstFlowReturn res = GST_FLOW_OK;
  GstBuffer * recbuffer = NULL;
  GstBuffer * play_buffer = NULL;
  gint bufsize;
  gchar * buf = NULL;
  GstClockTime duration;
  GstSpeexEchoProbe * probe = NULL;
  gint rate = 0;
  GstClockTime base_time;

  base_time = gst_element_get_base_time (GST_ELEMENT_CAST (self));

  GST_OBJECT_LOCK (self);
  if (self->probe)
    probe = g_object_ref (self->probe);
  rate = self->rate;
  GST_OBJECT_UNLOCK (self);

  bufsize = 2 * rate * self->frame_size_ms / 1000;
  duration = self->frame_size_ms * GST_MSECOND;

  if (gst_adapter_available (self->rec_adapter) == 0) {
    GST_LOG_OBJECT (self, "The adapter is empty, its a good time to reset the"
        " timestamp and offset");
    self->rec_time = GST_CLOCK_TIME_NONE;
    self->rec_offset = GST_BUFFER_OFFSET_NONE;
  }

  if (self->rec_time == GST_CLOCK_TIME_NONE)
    self->rec_time = GST_BUFFER_TIMESTAMP (buffer);
  if (self->rec_offset == GST_BUFFER_OFFSET_NONE)
    self->rec_offset = GST_BUFFER_OFFSET (buffer);

  // TODO: handle gaps
  gst_adapter_push (self->rec_adapter, buffer);

  while ((recbuffer = gst_adapter_take_buffer (self->rec_adapter,
      bufsize)) != NULL)
  {
    GstBuffer * outbuffer = NULL;
    GstClockTime play_rt = 0, rec_rt = 0, rec_end = 0;
    gint play_latency = 0;
    gint rec_offset;

    GST_BUFFER_TIMESTAMP (recbuffer) = self->rec_time;
    GST_BUFFER_OFFSET (recbuffer) = self->rec_offset;
    GST_BUFFER_DURATION (recbuffer) = duration;

    //play_latency = 200 * 1000000;
    rec_rt = gst_segment_to_running_time (&self->rec_segment, GST_FORMAT_TIME,
        self->rec_time) - play_latency;
    //rec_rt += 200 * 1000000;

    if (!self->echostate)
    {
      //printf("no echostate\n");
      outbuffer = gst_buffer_make_writable (recbuffer);
      gst_buffer_set_caps (outbuffer, GST_PAD_CAPS (self->rec_sinkpad));

      goto no_echo;
    }

    if (probe)
    {
      GST_OBJECT_LOCK (probe);
      play_latency = probe->latency;

      play_buffer = g_queue_peek_tail (probe->buffers);
      if (play_buffer)
        play_rt = GST_BUFFER_TIMESTAMP (play_buffer) - base_time; // FIXME
      GST_OBJECT_UNLOCK (probe);
    } else {
      GST_LOG_OBJECT (self, "No probe, not doing echo cancellation");
      outbuffer = gst_buffer_make_writable (recbuffer);
      gst_buffer_set_caps (outbuffer, GST_PAD_CAPS (self->rec_sinkpad));
      goto no_echo;
    }

    if (!play_buffer) {
      GST_LOG_OBJECT (self, "No playout buffer, not doing echo cancellation");
      outbuffer = gst_buffer_make_writable (recbuffer);
      gst_buffer_set_caps (outbuffer, GST_PAD_CAPS (self->rec_sinkpad));
      goto no_echo;
    }

    if (play_latency < 0)
    {
      GST_LOG_OBJECT (self, "Don't have latency, not cancelling echo");
      outbuffer = gst_buffer_make_writable (recbuffer);
      gst_buffer_set_caps (outbuffer, GST_PAD_CAPS (self->rec_sinkpad));
      goto no_echo;
    }

    if (play_latency > rec_rt ||
        play_rt + play_latency > rec_rt + duration)
    {
      GST_LOG_OBJECT (self, "Have no buffers to compare to,"
          " not cancelling echo");
      outbuffer = gst_buffer_make_writable (recbuffer);
      gst_buffer_set_caps (outbuffer, GST_PAD_CAPS (self->rec_sinkpad));
      goto no_echo;
    }

    res = gst_pad_alloc_buffer (self->rec_srcpad, self->rec_offset,
        bufsize, GST_PAD_CAPS (self->rec_sinkpad), &outbuffer);
    if (res != GST_FLOW_OK)
    {
      gst_buffer_unref (recbuffer);
      goto out;
    }

    g_assert (outbuffer);

    GST_BUFFER_TIMESTAMP (outbuffer) = GST_BUFFER_TIMESTAMP (recbuffer);
    GST_BUFFER_OFFSET (outbuffer) =  GST_BUFFER_OFFSET (recbuffer);
    GST_BUFFER_DURATION (outbuffer) = GST_BUFFER_DURATION (recbuffer);

    if (self->rec_time != GST_CLOCK_TIME_NONE)
      self->rec_time += self->frame_size_ms * GST_MSECOND;
    if (self->rec_offset != GST_BUFFER_OFFSET_NONE)
      self->rec_offset += bufsize;

    if (buf)
      memset (buf, 0, bufsize);
    else
      buf = g_malloc0 (bufsize);

    rec_end = rec_rt + duration;

    rec_offset = 0;

    GST_OBJECT_LOCK (probe);

    while (rec_offset < bufsize)
    {
      GstClockTime play_duration;
      gint play_offset, size;

      play_buffer = g_queue_peek_tail (probe->buffers);
      if (!play_buffer) {
        GST_LOG_OBJECT (self, "Queue empty, can't cancel everything");
        break;
      }

      play_rt = GST_BUFFER_TIMESTAMP (play_buffer) - base_time; // FIXME

      if (rec_end < play_rt) {
        GST_LOG_OBJECT (self, "End of recorded buffer (at %"GST_TIME_FORMAT")"
            " is before any played buffer"
            " (which start at %"GST_TIME_FORMAT")",
            GST_TIME_ARGS (rec_end),
            GST_TIME_ARGS (play_rt));

        break;
      }

      play_duration = GST_BUFFER_SIZE (play_buffer) * GST_SECOND /
          (rate * 2);

      if (play_rt + play_duration < rec_rt)
      {
        GST_LOG_OBJECT (self, "Start of rec data (at %"GST_TIME_FORMAT")"
            " after the end of played data (at %"GST_TIME_FORMAT")",
            GST_TIME_ARGS (rec_rt),
            GST_TIME_ARGS (play_rt + play_duration));
        g_queue_pop_tail (probe->buffers);
        gst_buffer_unref (play_buffer);
        continue;
      }

      if (rec_rt > play_rt) {
        GstClockTime time_diff = rec_rt - play_rt;
        time_diff *= 2 * rate;
        time_diff /= GST_SECOND;

        play_offset = time_diff;
        GST_LOG_OBJECT (self, "rec>play off: %d", play_offset);
      }
      else {
        GstClockTime time_diff = play_rt - rec_rt;
        time_diff *= 2 * rate;
        time_diff /= GST_SECOND;

        rec_offset += time_diff;
        rec_rt = play_rt;
        play_offset = 0;
        GST_LOG_OBJECT (self, "rec<=play off: %llu", time_diff);
      }

      play_offset = MIN (play_offset, GST_BUFFER_SIZE (play_buffer));

      size = MIN (GST_BUFFER_SIZE (play_buffer) - play_offset,
          bufsize - rec_offset);

      {
        GstClockTime time = play_offset;
        time *= GST_SECOND;
        time /= rate * 2;
        time += play_rt;

        GST_LOG_OBJECT (self, "Cancelling data recorded at %"GST_TIME_FORMAT
            " with data played at %"GST_TIME_FORMAT
            " (difference %"GST_TIME_FORMAT") for %d bytes",
            GST_TIME_ARGS (rec_rt),
            GST_TIME_ARGS (time),
            GST_TIME_ARGS (rec_rt - time),
            size);
      }

      memcpy (buf + rec_offset, GST_BUFFER_DATA (play_buffer) + play_offset,
          size);

      rec_rt += size * GST_SECOND / (rate * 2);
      rec_offset += size;

      if (GST_BUFFER_SIZE (play_buffer) == play_offset + size)
      {
        GstBuffer *pb = g_queue_pop_tail (probe->buffers);
        gst_buffer_unref (play_buffer);
        g_assert (pb == play_buffer);
      }
    }

    GST_OBJECT_UNLOCK (probe);

    GST_LOG_OBJECT (self, "Cancelling echo");

    speex_echo_cancellation (self->echostate,
        (const spx_int16_t *) GST_BUFFER_DATA (recbuffer),
        (const spx_int16_t *) buf,
        (spx_int16_t *) GST_BUFFER_DATA (outbuffer));

    gst_buffer_unref (recbuffer);

 no_echo:
    GST_OBJECT_LOCK (self);
    speex_preprocess_run (self->preprocstate,
        (spx_int16_t *) GST_BUFFER_DATA (outbuffer));
    GST_OBJECT_UNLOCK (self);

    GST_LOG_OBJECT (self, "Sending out buffer %p", outbuffer);

    res = gst_pad_push (self->rec_srcpad, outbuffer);

    if (res != GST_FLOW_OK)
      break;
  }

  g_free (buf);

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
    gst_speex_dsp_attach (GST_SPEEX_DSP (global_dsp), GST_SPEEX_ECHO_PROBE (global_probe));
    GST_DEBUG_OBJECT (global_dsp, "speexdsp attaching to globally discovered speexechoprobe");
  }
}

void
gst_speex_dsp_set_auto_attach (GstSpeexDSP * self, gboolean enabled)
{
  g_static_mutex_lock (&global_mutex);
  if (enabled) {
    if (!global_dsp) {
      global_dsp = G_OBJECT (self);
      try_auto_attach ();
    }
  }
  else {
    if (global_dsp == G_OBJECT (self))
      global_dsp = NULL;
  }
  g_static_mutex_unlock (&global_mutex);
}

/* global_mutex locked during this call */
void
gst_speex_dsp_attach (GstSpeexDSP * self, GstSpeexEchoProbe * probe)
{
  g_object_ref (probe);
  self->probe = probe;
  gst_speex_echo_probe_capture_start (self->probe);
}

/* global_mutex locked during this call */
void
gst_speex_dsp_detach (GstSpeexDSP * self)
{
  if (self->probe) {
    gst_speex_echo_probe_capture_stop (self->probe);
    g_object_unref (self->probe);
    self->probe = NULL;
  }
}
