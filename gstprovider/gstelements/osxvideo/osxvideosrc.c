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

#include <string.h>
#include "osxvideosrc.h"

#define WIDTH  640
#define HEIGHT 480
#define FRAMERATE ((float)30/1)

/*
QuickTime notes:

EnterMovies
  initialize QT subsystem
  there is no deinit

OpenDefaultComponent of type SeqGrabComponentType
  this gets a handle to a sequence grabber

CloseComponent
  release the sequence grabber

SGInitialize
  initialize the SG
  there is no deinit, simply close the component

SGSetDataRef of seqGrabDontMakeMovie
  this is to disable file creation.  we only want frames

SGNewChannel of VideoMediaType
  make a video capture channel

QTNewGWorld
  specify format (e.g. k32ARGBPixelFormat)
  specify size

LockPixels
  this makes it so the base address of the image doesn't "move".
  you can UnlockPixels also, if you care to.

CocoaSequenceGrabber locks (GetPortPixMap(gWorld)) for the entire session.
it also locks/unlocks the pixmaphandle
  [ PixMapHandle pixMapHandle = GetGWorldPixMap(gworld); ]
during the moment where it extracts the frame from the gworld

SGSetGWorld
  assign the gworld to the component
  pass GetMainDevice() as the last arg, which is just a formality?

SGSetChannelBounds
  use this to set our desired capture size.  the camera might not actually
    capture at this size, but it will pick something close.

SGSetChannelUsage of seqGrabRecord
  enable recording

SGSetDataProc
  set callback handler

SGPrepare
  prepares for recording.  this initializes the camera (the light should
  come on) so that when you call SGStartRecord you hit the ground running.
  maybe we should call SGPrepare when READY->PAUSED happens?

SGStartRecord
  kick off the recording

SGStop
  stop recording

SGGetChannelSampleDescription
  obtain the size the camera is actually capturing at

DecompressSequenceBegin
  i'm pretty sure you have to use this to receive the raw frames.
  you can also use it to scale the image.  to scale, create a matrix
    from the source and desired sizes and pass the matrix to this function.
  *** deprecated: use DecompressSequenceBeginS instead

CDSequenceEnd
  stop a decompress sequence

DecompressSequenceFrameS
  use this to obtain a raw frame.  the result ends up in the gworld
  *** deprecated: use DecompressSequenceFrameWhen instead

SGGetChannelDeviceList of sgDeviceListIncludeInputs
  obtain the list of devices for the video channel

SGSetChannelDevice
  set the master device (DV, USB, etc) on the channel, by string name

SGSetChannelDeviceInput
  set the sub device on the channel (iSight), by integer id
  device ids should be a concatenation of the above two values.

*/

GST_DEBUG_CATEGORY (gst_debug_osx_video_src);
#define GST_CAT_DEFAULT gst_debug_osx_video_src

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_DEVICE_NAME
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) UYVY, "
        //"width = (int) [ 1, MAX ], "
        //"height = (int) [ 1, MAX ], "
        //"framerate = (fraction) 0/1")
        "width = (int) 640, "
        "height = (int) 480, "
        "framerate = (fraction) 30/1")
    );

GST_BOILERPLATE (GstOSXVideoSrc, gst_osx_video_src, GstPushSrc,
    GST_TYPE_PUSH_SRC);

static void gst_osx_video_src_dispose (GObject * object);
static void gst_osx_video_src_finalize (GstOSXVideoSrc * osx_video_src);
static void gst_osx_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_osx_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_osx_video_src_change_state (
    GstElement * element, GstStateChange transition);

static gboolean gst_osx_video_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_osx_video_src_start (GstBaseSrc * src);
static gboolean gst_osx_video_src_stop (GstBaseSrc * src);
static gboolean gst_osx_video_src_query (GstBaseSrc * bsrc, GstQuery * query);
static GstFlowReturn gst_osx_video_src_create (GstPushSrc * src,
    GstBuffer ** buf);

static gboolean init_components (GstOSXVideoSrc * self);
static gboolean init_gworld (GstOSXVideoSrc * self);
static gboolean select_device (GstOSXVideoSrc * self, guint device_index);
static gboolean select_resolution (GstOSXVideoSrc * self, short width,
    short height);
static gboolean select_framerate (GstOSXVideoSrc * self, gfloat rate);
static gboolean prepare_capture (GstOSXVideoSrc * self);

// ### rewrite these function using glib-isms.  also, escape ':' in case it
//   appears in sgname
static gchar *
create_device_id (const gchar * sgname, int inputIndex)
{
  gchar * out;
  gchar inputstr[3];
  int len1, len2;

  if (inputIndex < 0 || inputIndex > 99)
    return NULL;

  g_sprintf (inputstr, "%d", inputIndex);
  len1 = strlen (sgname);
  len2 = strlen (inputstr);

  out = g_malloc (len1 + 1 + len2 + 1);
  if (!out)
    return NULL;

  memcpy (out, sgname, len1);
  out[len1] = ':';
  memcpy (out + len1 + 1, inputstr, len2);
  out[len1 + 1 + len2] = 0;
  return out;
}

static gboolean
parse_device_id (const gchar * id, gchar ** sgname, int * inputIndex)
{
  gchar * p;
  gchar * out1;
  int len1;
  int out2;

  p = g_strrstr (id, ":");
  if (p) {
    len1 = p - id;
    out2 = strtol (p + 1, NULL, 10);
    if (out2 == 0 && (errno == ERANGE || errno == EINVAL))
      return FALSE;
    out1 = g_malloc (len1 + 1);
    if (!out1)
      return FALSE;
    memcpy (out1, id, len1);
    out1[len1] = 0;
  }
  else {
    len1 = strlen (id);
    out2 = 0;
    out1 = g_malloc (len1 + 1);
    if (!out1)
      return FALSE;
    memcpy (out1, id, len1);
    out1[len1] = 0;
  }

  *sgname = out1;
  *inputIndex = out2;
  return TRUE;
}

static gchar * str63_to_str (const Str63 in)
{
  gchar *ret;
  unsigned char len;

  len = in[0];
  ret = g_malloc (len + 1);
  if (!ret)
    return NULL;

  memcpy (ret, in + 1, len);
  ret[len] = 0;
  return ret;
}

static gboolean device_set_default (GstOSXVideoSrc * src)
{
  SeqGrabComponent component;
  SGChannel channel;
  SGDeviceList deviceList;
  SGDeviceName * deviceEntry;
  SGDeviceInputList inputList;
  SGDeviceInputName * inputEntry;
  ComponentResult err;
  int n, i;
  gchar * sgname;
  int inputIndex;
  gchar * friendly_name;

  component = OpenDefaultComponent (SeqGrabComponentType, 0);
  if (!component)
    return FALSE;

  if (SGInitialize (component) != noErr) {
    CloseComponent (component);
    return FALSE;
  }

  if (SGSetDataRef (component, 0, 0, seqGrabDontMakeMovie) != noErr) {
    CloseComponent (component);
    return FALSE;
  }

  err = SGNewChannel (component, VideoMediaType, &channel);
  if (err != noErr) {
    CloseComponent (component);
    return FALSE;
  }

  if (SGGetChannelDeviceList (channel, sgDeviceListIncludeInputs, &deviceList) != noErr) {
    CloseComponent (component);
    return FALSE;
  }

  deviceEntry = &(*deviceList)->entry[(*deviceList)->selectedIndex];
  inputList = deviceEntry->inputs;

  sgname = str63_to_str (deviceEntry->name);

  if (inputList && (*inputList)->count >= 1) {
    inputIndex = (*inputList)->selectedIndex;
    inputEntry = &(*inputList)->entry[inputIndex];
    friendly_name = str63_to_str (inputEntry->name);
  }
  else {
    /* ### can a device have no defined inputs? */
    inputIndex = 0;
    friendly_name = g_strdup (sgname);
  }

  src->device_id = create_device_id (sgname, inputIndex);
  if (!src->device_id) {
    g_free (sgname);
    g_free (friendly_name);
    return FALSE;
  }
  g_free (sgname);
  sgname = NULL;

  src->device_name = friendly_name;

  return TRUE;
}

static gboolean device_get_name (GstOSXVideoSrc * src)
{
  // ### this should work for specified devices as well, not just default
  if (!src->device_id)
    return device_set_default (src);

  return TRUE;
}

static gboolean device_select (GstOSXVideoSrc * src)
{
  // FIXME
  //if (!src->device_id && !device_set_default (src))
  //    return FALSE;

  return TRUE;
}

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
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_osx_video_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_osx_video_src_get_property);

  element_class->change_state = gst_osx_video_src_change_state;

  basesrc_class->set_caps = gst_osx_video_src_set_caps;
  basesrc_class->start = gst_osx_video_src_start;
  basesrc_class->stop = gst_osx_video_src_stop;
  basesrc_class->query = gst_osx_video_src_query;

  pushsrc_class->create = gst_osx_video_src_create;

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "Sequence Grabber input device in format 'sgname:input#'",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the video device",
          NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

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
  self->seq_num = 0;

  // ###: moved this elsewhere
  //if (!init_components (self))
    //return;

  // ###
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);

  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);

  gst_pad_use_fixed_caps (srcpad);
}

static void
gst_osx_video_src_dispose (GObject * object)
{
  GstOSXVideoSrc * self = GST_OSX_VIDEO_SRC (object);
  GST_DEBUG_OBJECT (object, G_STRFUNC);

  if (self->device_id) {
    g_free (self->device_id);
    self->device_id = NULL;
  }

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

static void
gst_osx_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOSXVideoSrc * src = GST_OSX_VIDEO_SRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
      if (src->device_id)
        g_free (src->device_id);
      src->device_id = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osx_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOSXVideoSrc * src = GST_OSX_VIDEO_SRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
      if (!src->device_id)
        device_set_default (src);
      g_value_set_string (value, src->device_id);
      break;
    case ARG_DEVICE_NAME:
      if (!src->device_name)
        device_get_name (src);
      g_value_set_string (value, src->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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

  // ### disabling this for now
  //if (!select_resolution(self, width, height))
  //  return FALSE;

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
  self->seq_num = 0;

  if (!init_components (self))
    return FALSE;

  if (!init_gworld (self))
    return FALSE;

  select_resolution(self, 640, 480);

  //if (!device_select (self))
    //return FALSE;


  //if (!select_device (self, 0))
    //return FALSE;

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

static gboolean
gst_osx_video_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstOSXVideoSrc * self;
  gboolean res = FALSE;

  self = GST_OSX_VIDEO_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;
      gint fps_n, fps_d;

      fps_n = 30;
      fps_d = 1;

      /* min latency is the time to capture one frame */
      min_latency = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);

      /* max latency is total duration of the frame buffer */
      // FIXME: we don't know what this is, so we'll just say 2 frames
      max_latency = 2 * min_latency;

      GST_DEBUG_OBJECT (bsrc,
          "report latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      /* we are always live, the min latency is 1 frame and the max latency is
       * the complete buffer of frames. */
      gst_query_set_latency (query, TRUE, min_latency, max_latency);

      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

done:
  return res;
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

        {
          ImageDescriptionHandle imageDesc = (ImageDescriptionHandle) NewHandle (0);
          ComponentResult err;
          Rect sourceRect;
          MatrixRecord scaleMatrix;

          err = SGGetChannelSampleDescription (self->video_chan, (Handle) imageDesc);
          if (err != noErr) {
            // TODO
          }

          sourceRect.top = 0;
          sourceRect.left = 0;
          sourceRect.right = (**imageDesc).width;
          sourceRect.bottom = (**imageDesc).height;

          RectMatrix(&scaleMatrix, &sourceRect, &self->rect);

          err = DecompressSequenceBegin (&self->dec_seq, imageDesc, self->world, NULL, NULL, &scaleMatrix, srcCopy, NULL, 0, codecNormalQuality, bestSpeedCodec);
          if (err != noErr) {
            // TODO
          }

          DisposeHandle ((Handle) imageDesc);
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
  GstOSXVideoSrc * self;
  gint fps_n, fps_d;
  GstClockTime duration, timestamp, latency;

  self = GST_OSX_VIDEO_SRC (refCon);

  printf("data_proc\n");

  if (self->buffer != NULL) {
    gst_buffer_unref (self->buffer);
    self->buffer = NULL;
  }

  if (self->world) {
    CodecFlags flags;
    ComponentResult err;
    PixMapHandle hPixMap;
    Rect portRect;
    int pix_rowBytes;
    void *pix_ptr;
    int pix_height;
    int pix_size;

    err = DecompressSequenceFrameS (self->dec_seq, p, len, 0, &flags, NULL);
    if (err != noErr) {
      // TODO
      printf("error decompressing\n");
    }

    hPixMap = GetGWorldPixMap (self->world);
    LockPixels (hPixMap);
    GetPortBounds (self->world, &portRect);
    pix_rowBytes = (int) GetPixRowBytes (hPixMap);
    pix_ptr = GetPixBaseAddr (hPixMap);
    pix_height = (portRect.bottom - portRect.top);
    pix_size = pix_rowBytes * pix_height;

    printf("num=%5d, height=%d, rowBytes=%d, size=%d\n", self->seq_num, pix_height, pix_rowBytes, pix_size);

    fps_n = 30;
    fps_d = 1;

    duration = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    latency = duration;

    timestamp = gst_clock_get_time (GST_ELEMENT_CAST (self)->clock);
    timestamp -= gst_element_get_base_time (GST_ELEMENT_CAST (self));
    if (timestamp > latency)
      timestamp -= latency;
    else
      timestamp = 0;

    self->buffer = gst_buffer_new_and_alloc (pix_size);
    GST_BUFFER_OFFSET (self->buffer) = self->seq_num;
    GST_BUFFER_TIMESTAMP (self->buffer) = timestamp;
    //GST_BUFFER_DURATION (self->buffer) = duration;
    memcpy (GST_BUFFER_DATA (self->buffer), pix_ptr, pix_size);

    self->seq_num++;

    UnlockPixels (hPixMap);
  }

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
