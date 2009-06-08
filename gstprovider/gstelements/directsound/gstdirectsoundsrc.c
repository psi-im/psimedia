/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007-2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 * Copyright (C) 2009 Barracuda Networks, Inc.
 *
 * gstdirectsoundsrc.c:
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdirectsoundsrc.h"

#define GST_CAT_DEFAULT directsound

/* elementfactory information */
static const GstElementDetails gst_directsound_src_details =
GST_ELEMENT_DETAILS ("DirectSound8 Audio Source",
    "Source/Audio",
    "Input from a sound card via DirectSound8",
    "Justin Karneges <justin@affinix.com>");

static GstStaticPadTemplate directsoundsrc_src_factory =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) TRUE, "
        "width = (int) {8, 16}, "
        "depth = (int) {8, 16}, "
        "rate = (int) [ 1, MAX ], " 
        "channels = (int) [ 1, 2 ]"));

static void gst_directsound_src_base_init (gpointer g_class);
static void gst_directsound_src_class_init (GstDirectSoundSrcClass * klass);
static void gst_directsound_src_init (GstDirectSoundSrc * dsoundsrc,
    GstDirectSoundSrcClass * g_class);

static gboolean gst_directsound_src_event (GstBaseSrc * bsrc,
    GstEvent * event);

static void gst_directsound_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_directsound_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstRingBuffer * gst_directsound_src_create_ringbuffer (
    GstBaseAudioSrc * src);

enum
{
  ARG_0
  //ARG_VOLUME
};

GST_BOILERPLATE (GstDirectSoundSrc, gst_directsound_src, GstBaseAudioSrc,
    GST_TYPE_BASE_AUDIO_SRC);

static void
gst_directsound_src_base_init (gpointer g_class)
{
  GstElementClass * element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class,
      &gst_directsound_src_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&directsoundsrc_src_factory));
}

static void
gst_directsound_src_class_init (GstDirectSoundSrcClass * klass)
{
  GObjectClass * gobject_class;
  GstElementClass * gstelement_class;
  GstBaseSrcClass * gstbasesrc_class;
  GstBaseAudioSrcClass * gstbaseaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_directsound_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_directsound_src_get_property);

  /*g_object_class_install_property (gobject_class, ARG_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0, 1.0, 1.0, G_PARAM_READWRITE));*/

  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_directsound_src_event);

  gstbaseaudiosrc_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_directsound_src_create_ringbuffer);
}

static void
gst_directsound_sink_init (GstDirectSoundSrc * dsoundsrc,
    GstDirectSoundSrcClass * g_class)
{
  dsoundsrc->dsoundbuffer = NULL;
  dsoundsrc->volume = 1.0;
}

static gboolean
gst_directsound_src_event (GstBaseSink * bsrc, GstEvent * event)
{
  HRESULT hr;
  DWORD dwStatus;
  DWORD dwSizeBuffer = 0;
  LPVOID pLockedBuffer = NULL;

  GstDirectSoundSrc * dsoundsrc;

  dsoundsrc = GST_DIRECTSOUND_SRC (bsrc);

  GST_BASE_SRC_CLASS (parent_class)->event (bsrc, event);

  /* no buffer, no event to process */
  if (!dsoundsrc->dsoundbuffer)
    return TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DSOUND_LOCK (dsoundsrc->dsoundbuffer);
      dsoundsrc->dsoundbuffer->flushing = TRUE;
      GST_DSOUND_UNLOCK (dsoundsrc->dsoundbuffer);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DSOUND_LOCK (dsoundsrc->dsoundbuffer);
      dsoundsrc->dsoundbuffer->flushing = FALSE;

      if (dsoundsrc->dsoundbuffer->pDSCB8) {
        hr = IDirectSoundCaptureBuffer8_GetStatus (dsoundsrc->dsoundbuffer->pDSCB8, &dwStatus);

        if (FAILED(hr)) {
          GST_DSOUND_UNLOCK (dsoundsrc->dsoundbuffer);
          GST_WARNING("gst_directsound_src_event: IDirectSoundCaptureBuffer8_GetStatus, hr = %X", (unsigned int) hr);
          return FALSE;
        }

        if (!(dwStatus & DSCBSTATUS_CAPTURING)) {
          // ###: capture api doesn't support _SetCurrentPosition.  commenting
          //   out for now.
#if 0
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
#endif
        }
      }
      GST_DSOUND_UNLOCK (dsoundsrc->dsoundbuffer);
      break;
    default:
      break;
  }

  return TRUE;
}

/*static void
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
}*/

static void
gst_directsound_src_set_property (GObject * object,
    guint prop_id, const GValue * value , GParamSpec * pspec)
{
  GstDirectSoundSrc * src = GST_DIRECTSOUND_SRC (object);

  switch (prop_id) {
    /*case ARG_VOLUME:
      sink->volume = g_value_get_double (value);
      gst_directsound_sink_set_volume (sink);
      break;*/
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directsound_src_get_property (GObject * object,
    guint prop_id, GValue * value , GParamSpec * pspec)
{
  GstDirectSoundSrc * src = GST_DIRECTSOUND_SRC (object);

  switch (prop_id) {
    /*case ARG_VOLUME:
      g_value_set_double (value, sink->volume);
      break;*/
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseAudioSrc vmethod implementations */
static GstRingBuffer *
gst_directsound_src_create_ringbuffer (GstBaseAudioSrc * src)
{
  GstDirectSoundSrc * dsoundsrc;
  GstDirectSoundRingBuffer * ringbuffer;

  dsoundsrc = GST_DIRECTSOUND_SRC (src);

  GST_DEBUG ("creating ringbuffer");
  ringbuffer = g_object_new (GST_TYPE_DIRECTSOUND_RING_BUFFER, NULL);
  GST_DEBUG ("directsound src 0x%p", dsoundsrc);

  /* capture */
  ringbuffer->is_src = TRUE;

  /* set the src element on the ringbuffer for error messages */
  ringbuffer->element = GST_ELEMENT (dsoundsrc);

  /* set the ringbuffer on the src */
  dsoundsrc->dsoundbuffer = ringbuffer;

  /* set initial volume on ringbuffer */
  dsoundsrc->dsoundbuffer->volume = dsoundsrc->volume;

  return GST_RING_BUFFER (ringbuffer);
}
