/*
 * GStreamer
 * Copyright 2007 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright 2007 Ali Sabil <ali.sabil@tandberg.com>
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
 */

/**
 * SECTION:element-osxvideosrc
 *
 * <refsect2>
 * osxvideosrc can be used to capture video from capture devices on OS X.
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch osxvideosrc ! osxvideosink
 * </programlisting>
 * This pipeline shows the video captured from the default capture device.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "osxvideosrc.h"

GST_DEBUG_CATEGORY (gst_debug_osx_video_src);
#define GST_CAT_DEFAULT gst_debug_osx_video_src

#define WIDTH 640
#define HEIGHT 480
#define FRAMERATE ((float)(30/1))

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) UYVY, "
        "width = (int) 640, "
        "height = (int) 480, "
        "framerate = (fraction) 30/1")
    );

GST_BOILERPLATE (GstOSXVideoSrc, gst_osx_video_src, GstPushSrc,
    GST_TYPE_PUSH_SRC);

static void gst_osx_video_src_dispose (GObject * object);
static void gst_osx_video_src_finalize (GstOSXVideoSrc * osx_video_src);

static gboolean gst_osx_video_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_osx_video_src_start (GstBaseSrc * src);
static gboolean gst_osx_video_src_stop (GstBaseSrc * src);
static GstStateChangeReturn gst_osx_video_src_change_state (
    GstElement * element, GstStateChange transition);
static GstFlowReturn gst_osx_video_src_create (GstPushSrc * src,
    GstBuffer ** buf);

static gboolean init_components (GstOSXVideoSrc * self);
static gboolean init_gworld (GstOSXVideoSrc * self);
static gboolean select_device (GstOSXVideoSrc * self, guint device_index);
static gboolean select_resolution (GstOSXVideoSrc * self, short width,
    short height);
static gboolean select_framerate (GstOSXVideoSrc * self, gfloat rate);
static gboolean prepare_capture(GstOSXVideoSrc * self);

static void
gst_osx_video_src_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
      "Video Source (OSX)",
      "Source/Video",
      "Reads raw frames from a capture device on OS X",
      "Ole Andre Vadla Ravnaas <ole.andre.ravnas@tandberg.com>, "
      "Ali Sabil <ali.sabil@tandberg.com>"
  };

  GstElementClass * element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG (G_STRFUNC);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the plugin's class */
static void
gst_osx_video_src_class_init (GstOSXVideoSrcClass * klass)
{
  GObjectClass * gobject_class;
  GstElementClass * element_class;
  GstBaseSrcClass * basesrc_class;
  GstPushSrcClass * pushsrc_class;

  GST_DEBUG (G_STRFUNC);

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basesrc_class = GST_BASE_SRC_CLASS (klass);
  pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->dispose = gst_osx_video_src_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_osx_video_src_finalize;

  element_class->change_state = gst_osx_video_src_change_state;

  basesrc_class->set_caps = gst_osx_video_src_set_caps;
  basesrc_class->start = gst_osx_video_src_start;
  basesrc_class->stop = gst_osx_video_src_stop;

  pushsrc_class->create = gst_osx_video_src_create;

  EnterMovies();
}

static void
gst_osx_video_src_init (GstOSXVideoSrc * self, GstOSXVideoSrcClass * klass)
{
  GstPad * srcpad = GST_BASE_SRC_PAD (self);

  GST_DEBUG_OBJECT (self, G_STRFUNC);

  SetRect(&self->rect, 0, 0, WIDTH, HEIGHT);
  self->world = NULL;
  self->buffer = NULL;
  self->seq = 0;

  if (!init_components (self))
    return;

  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);

  gst_pad_use_fixed_caps (srcpad);
  /*gst_pad_set_caps (srcpad, gst_caps_copy (
        gst_pad_get_pad_template_caps (srcpad)));
  gst_pad_set_setcaps_function (srcpad, gst_osx_video_src_set_caps);*/
}

static void
gst_osx_video_src_dispose (GObject * object)
{
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (object);
  GST_DEBUG_OBJECT (object, G_STRFUNC);

  if (self->buffer != NULL) {
    gst_buffer_unref (self->buffer);
    self->buffer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_osx_video_src_finalize (GstOSXVideoSrc * self)
{
  GST_DEBUG_OBJECT (self, G_STRFUNC);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (self));
}

static gboolean
gst_osx_video_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (src);
  GstStructure * structure = gst_caps_get_structure (caps, 0);
  gint width, height, framerate_num, framerate_denom;

  GST_DEBUG_OBJECT (src, G_STRFUNC);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_fraction (structure, "framerate", &framerate_num, &framerate_denom);

  GST_DEBUG_OBJECT (src, "changing caps to %dx%d@%f", width, height, (float) (framerate_num / framerate_denom));

  if (!select_resolution(self, width, height))
    return FALSE;

  /*if (!select_framerate(self, (float) (framerate_num / framerate_denom)))
    return FALSE;*/

  return TRUE;
}

static gboolean
gst_osx_video_src_start (GstBaseSrc * src)
{
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (src, "entering");

  if (self->buffer != NULL) {
    gst_buffer_unref (self->buffer);
    self->buffer = NULL;
  }
  self->seq = 0;

  if (!init_gworld (self))
    return FALSE;

  if (!select_device (self, 0))
    return FALSE;

  /*if (!select_resolution (self, WIDTH, HEIGHT))
    return FALSE;

  if (!select_framerate (self, FRAMERATE))
    return FALSE;
  }*/

  GST_DEBUG_OBJECT (self, "started");
  return TRUE;
}

static gboolean
gst_osx_video_src_stop (GstBaseSrc * src)
{
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (src, "stopping");

  if (self->world != NULL)
    DisposeGWorld (self->world);

  if (self->buffer != NULL) {
    gst_buffer_unref (self->buffer);
    self->buffer = NULL;
  }
  return TRUE;
}

static GstStateChangeReturn
gst_osx_video_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        if (!prepare_capture(self))
          return GST_STATE_CHANGE_FAILURE;

        if (SGStartRecord (self->seq_grab) != noErr) {
          GST_ERROR_OBJECT (self, "SGStartRecord failed");
          return GST_STATE_CHANGE_FAILURE;
        }
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element,
      transition);
  if (result == GST_STATE_CHANGE_FAILURE)
    return result;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      SGStop (self->seq_grab);
      SGRelease (self->seq_grab);
    default:
      break;
  }

  return result;
}

static GstFlowReturn
gst_osx_video_src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (src);
  ComponentResult result;
  GstCaps * caps;
  GstClock * clock;

  clock = gst_system_clock_obtain ();
  do {
    if ((result = SGIdle (self->seq_grab)) != noErr) {
      GST_ERROR_OBJECT (self, "SGIdle failed with result %ld", result);
      gst_object_unref (clock);
      return GST_FLOW_UNEXPECTED;
    }

    if (self->buffer == NULL) {
      GstClockID clock_id;

      clock_id = gst_clock_new_single_shot_id (clock,
          (GstClockTime) (gst_clock_get_time(clock) +
          (GST_SECOND / (FRAMERATE * 2))));
      gst_clock_id_wait (clock_id, NULL);
      gst_clock_id_unref (clock_id);
    }

  } while (self->buffer == NULL);
  gst_object_unref (clock);

  *buf = self->buffer;
  self->buffer = NULL;

  caps = gst_pad_get_caps (GST_BASE_SRC_PAD (src));
  gst_buffer_set_caps (*buf, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}

static OSErr
data_proc (SGChannel c, Ptr p, long len, long * offset, long chRefCon,
    TimeValue time, short writeType, long refCon)
{
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (refCon);

  if (self->buffer != NULL) {
    gst_buffer_unref (self->buffer);
    self->buffer = NULL;
  }

  self->buffer = gst_buffer_new_and_alloc (len);
  GST_BUFFER_OFFSET (self->buffer) = self->seq;
  GST_BUFFER_TIMESTAMP (self->buffer) = gst_util_uint64_scale_int (
      self->seq, GST_SECOND, FRAMERATE);
  GST_BUFFER_DURATION (self->buffer) = gst_util_uint64_scale_int (
      1, GST_SECOND, FRAMERATE);
  memcpy (GST_BUFFER_DATA (self->buffer), p, len);
  self->seq++;

  return noErr;
}

static gboolean
init_components (GstOSXVideoSrc * self)
{
  ComponentResult result = noErr;

  self->seq_grab = OpenDefaultComponent (SeqGrabComponentType, 0);
  if (self->seq_grab == NULL) {
    GST_ERROR_OBJECT (self, "OpenDefaultComponent failed");
    result = paramErr;
    goto OUT;
  }

  result = SGInitialize (self->seq_grab);
  if (result != noErr) {
    GST_ERROR_OBJECT (self, "SGInitialize failed");
    goto OUT;
  }

  result = SGSetDataRef (self->seq_grab, 0, 0, seqGrabDontMakeMovie);
  if (result != noErr) {
    GST_ERROR_OBJECT (self, "SGSetDataRef failed");
    goto OUT;
  }

  result = SGNewChannel (self->seq_grab, VideoMediaType, &self->video_chan);
  if (result != noErr) {
    GST_ERROR_OBJECT (self, "SGNewChannel failed");
    goto OUT;
  }

OUT:
  return (result == noErr);
}

static gboolean
init_gworld (GstOSXVideoSrc * self)
{
  ComponentResult result = noErr;

  g_return_val_if_fail (self->world == NULL, FALSE);

  result = QTNewGWorld (&self->world, k422YpCbCr8PixelFormat,
      &self->rect, 0, NULL, 0);
  if (result != noErr) {
    GST_ERROR ("QTNewGWorld failed");
    return FALSE;
  }

  if (LockPixels (GetPortPixMap (self->world)) == 0) {
    GST_ERROR ("LockPixels failed");
    return FALSE;
  }

  result = SGSetGWorld (self->seq_grab, self->world, NULL);
  if (result != noErr) {
    GST_ERROR ("SGSetGWorld failed");
    return FALSE;
  }
  return TRUE;
}

static gboolean
select_device (GstOSXVideoSrc * self, guint device_index)
{
  // ComponentResult result = noErr;

  // FIXME: select the device, instead of ignoring the parameter

  return TRUE;
}

static gboolean
select_resolution (GstOSXVideoSrc * self, short width, short height)
{
  ComponentResult result = noErr;

  SetRect(&self->rect, 0, 0, width, height);

  result = SGSetChannelBounds (self->video_chan, &self->rect);
  if (result != noErr) {
    GST_ERROR ("SGSetChannelBounds failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
select_framerate (GstOSXVideoSrc * self, gfloat rate)
{
  ComponentResult result = SGSetFrameRate (self->video_chan,
      FloatToFixed(rate));
  if (result != noErr) {
    GST_ERROR ("SGSetFrameRate failed");
    return FALSE;
  }

  return TRUE;
}

static
gboolean prepare_capture(GstOSXVideoSrc * self)
{
  ComponentResult result = noErr;

  result = SGSetDataProc (self->seq_grab, NewSGDataUPP (data_proc),
      (long) self);
  if (result != noErr) {
    GST_ERROR_OBJECT (self, "SGSetDataProc failed");
    return FALSE;
  }

  result = SGSetChannelUsage (self->video_chan, seqGrabRecord);
  if (result != noErr) {
    GST_ERROR_OBJECT (self, "SGSetChannelUsage failed");
    return FALSE;
  }

  result = SGPrepare (self->seq_grab, false, true);
  if (result != noErr) {
    GST_ERROR_OBJECT (self, "SGPrepare failed");
    return FALSE;
  }
  return TRUE;
}
