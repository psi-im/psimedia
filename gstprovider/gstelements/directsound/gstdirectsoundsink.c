/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007-2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * gstdirectsoundsink.c:
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
 * The development of this code was made possible due to the involvement
 * of Pioneers of the Inevitable, the creators of the Songbird Music player
 *
 */

/**
 * SECTION:element-directsoundsink
 *
 * This element lets you output sound using the DirectSound API.
 *
 * Note that you should almost always use generic audio conversion elements
 * like audioconvert and audioresample in front of an audiosink to make sure
 * your pipeline works under all circumstances (those conversion elements will
 * act in passthrough-mode if no conversion is necessary).
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc ! audioconvert ! volume volume=0.1 ! directsoundsink
 * ]| will output a sine wave (continuous beep sound) to your sound card (with
 * a very low volume as precaution).
 * |[
 * gst-launch -v filesrc location=music.ogg ! decodebin ! audioconvert ! audioresample ! directsoundsink
 * ]| will play an Ogg/Vorbis audio file and output it.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdirectsoundsink.h"

#define GST_CAT_DEFAULT directsound

/* elementfactory information */
static const GstElementDetails gst_directsound_sink_details =
GST_ELEMENT_DETAILS ("DirectSound8 Audio Sink",
    "Sink/Audio",
    "Output to a sound card via DirectSound8",
    "Ghislain 'Aus' Lacroix <aus@songbirdnest.com>");

static GstStaticPadTemplate directsoundsink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) TRUE, "
        "width = (int) {8, 16}, "
        "depth = (int) {8, 16}, "
        "rate = (int) [ 1, MAX ], " 
        "channels = (int) [ 1, 2 ]"));

static void gst_directsound_sink_base_init (gpointer g_class);
static void gst_directsound_sink_class_init (GstDirectSoundSinkClass * klass);
static void gst_directsound_sink_init (GstDirectSoundSink * dsoundsink,
    GstDirectSoundSinkClass * g_class);

static gboolean gst_directsound_sink_event (GstBaseSink * bsink,
    GstEvent * event);

static void gst_directsound_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_directsound_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstRingBuffer * gst_directsound_sink_create_ringbuffer (
    GstBaseAudioSink * sink);

enum
{
  ARG_0,
  ARG_VOLUME
};

GST_BOILERPLATE (GstDirectSoundSink, gst_directsound_sink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK);

static void
gst_directsound_sink_base_init (gpointer g_class)
{
  GstElementClass * element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class,
      &gst_directsound_sink_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&directsoundsink_sink_factory));
}

static void
gst_directsound_sink_class_init (GstDirectSoundSinkClass * klass)
{
  GObjectClass * gobject_class;
  GstElementClass * gstelement_class;
  GstBaseSinkClass * gstbasesink_class;
  GstBaseAudioSinkClass * gstbaseaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_directsound_sink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_directsound_sink_get_property);

  g_object_class_install_property (gobject_class, ARG_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0, 1.0, 1.0, G_PARAM_READWRITE));

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_directsound_sink_event);

  gstbaseaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_directsound_sink_create_ringbuffer);
}

static void
gst_directsound_sink_init (GstDirectSoundSink * dsoundsink,
    GstDirectSoundSinkClass * g_class)
{
  dsoundsink->dsoundbuffer = NULL;
  dsoundsink->volume = 1.0;
}

static gboolean
gst_directsound_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  HRESULT hr;
  DWORD dwStatus;
  DWORD dwSizeBuffer = 0;
  LPVOID pLockedBuffer = NULL;

  GstDirectSoundSink * dsoundsink;

  dsoundsink = GST_DIRECTSOUND_SINK (bsink);

  GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);

  /* no buffer, no event to process */
  if (!dsoundsink->dsoundbuffer)
    return TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DSOUND_LOCK (dsoundsink->dsoundbuffer);
      dsoundsink->dsoundbuffer->flushing = TRUE;
      GST_DSOUND_UNLOCK (dsoundsink->dsoundbuffer);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DSOUND_LOCK (dsoundsink->dsoundbuffer);
      dsoundsink->dsoundbuffer->flushing = FALSE;

      if (dsoundsink->dsoundbuffer->pDSB8) {
        hr = IDirectSoundBuffer8_GetStatus (dsoundsink->dsoundbuffer->pDSB8, &dwStatus);

        if (FAILED(hr)) {
          GST_DSOUND_UNLOCK (dsoundsink->dsoundbuffer);
          GST_WARNING("gst_directsound_sink_event: IDirectSoundBuffer8_GetStatus, hr = %X", (unsigned int) hr);
          return FALSE;
        }

        if (!(dwStatus & DSBSTATUS_PLAYING)) {
          /* reset position */
          hr = IDirectSoundBuffer8_SetCurrentPosition (dsoundsink->dsoundbuffer->pDSB8, 0);
          dsoundsink->dsoundbuffer->buffer_circular_offset = 0;

          /* reset the buffer */
          hr = IDirectSoundBuffer8_Lock (dsoundsink->dsoundbuffer->pDSB8,
              dsoundsink->dsoundbuffer->buffer_circular_offset, 0L,
              &pLockedBuffer, &dwSizeBuffer, NULL, NULL, DSBLOCK_ENTIREBUFFER);

          if (SUCCEEDED (hr)) {
            memset (pLockedBuffer, 0, dwSizeBuffer);

            hr =
                IDirectSoundBuffer8_Unlock (dsoundsink->dsoundbuffer->pDSB8, pLockedBuffer,
                dwSizeBuffer, NULL, 0);

            if (FAILED(hr)) {
              GST_DSOUND_UNLOCK (dsoundsink->dsoundbuffer);
              GST_WARNING("gst_directsound_sink_event: IDirectSoundBuffer8_Unlock, hr = %X", (unsigned int) hr);
              return FALSE;
            }
          }
          else {
            GST_DSOUND_UNLOCK (dsoundsink->dsoundbuffer);
            GST_WARNING ( "gst_directsound_sink_event: IDirectSoundBuffer8_Lock, hr = %X", (unsigned int) hr);
            return FALSE;
          }
        }
      }
      GST_DSOUND_UNLOCK (dsoundsink->dsoundbuffer);
      break;
    default:
      break;
  }

  return TRUE;
}

static void
gst_directsound_sink_set_volume (GstDirectSoundSink * dsoundsink)
{
  if (dsoundsink->dsoundbuffer) {
    GST_DSOUND_LOCK (dsoundsink->dsoundbuffer);

    dsoundsink->dsoundbuffer->volume = dsoundsink->volume;
    if (dsoundsink->dsoundbuffer->pDSB8) {
      gst_directsound_set_volume (dsoundsink->dsoundbuffer->pDSB8,
          dsoundsink->volume);
    }
    GST_DSOUND_UNLOCK (dsoundsink->dsoundbuffer);
  }
}

static void
gst_directsound_sink_set_property (GObject * object,
    guint prop_id, const GValue * value , GParamSpec * pspec)
{
  GstDirectSoundSink * sink = GST_DIRECTSOUND_SINK (object);

  switch (prop_id) {
    case ARG_VOLUME:
      sink->volume = g_value_get_double (value);
      gst_directsound_sink_set_volume (sink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directsound_sink_get_property (GObject * object,
    guint prop_id, GValue * value , GParamSpec * pspec)
{
  GstDirectSoundSink * sink = GST_DIRECTSOUND_SINK (object);

  switch (prop_id) {
    case ARG_VOLUME:
      g_value_set_double (value, sink->volume);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseAudioSink vmethod implementations */
static GstRingBuffer *
gst_directsound_sink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstDirectSoundSink * dsoundsink;
  GstDirectSoundRingBuffer * ringbuffer;

  dsoundsink = GST_DIRECTSOUND_SINK (sink);

  GST_DEBUG ("creating ringbuffer");
  ringbuffer = g_object_new (GST_TYPE_DIRECTSOUND_RING_BUFFER, NULL);
  GST_DEBUG ("directsound sink 0x%p", dsoundsink);

  /* playback */
  ringbuffer->is_src = FALSE;

  /* set the sink element on the ringbuffer for error messages */
  ringbuffer->element = GST_ELEMENT (dsoundsink);

  /* set the ringbuffer on the sink */
  dsoundsink->dsoundbuffer = ringbuffer;

  /* set initial volume on ringbuffer */
  dsoundsink->dsoundbuffer->volume = dsoundsink->volume;

  return GST_RING_BUFFER (ringbuffer);
}
