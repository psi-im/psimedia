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
#include <gst/interfaces/propertyprobe.h>
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

SGRelease
  unprepare the recording

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

static void gst_osx_video_src_init_interfaces (GType type);
static void gst_osx_video_src_type_add_device_property_probe_interface (GType type);

GST_BOILERPLATE_FULL (GstOSXVideoSrc, gst_osx_video_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_osx_video_src_init_interfaces);

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

/* \ = \\, : = \c */
static GString *
escape_string (const GString * in)
{
  GString * out;
  int n;

  out = g_string_sized_new (64);
  for (n = 0; n < (int) in->len; ++n) {
    if (in->str[n] == '\\')
      g_string_append (out, "\\\\");
    else if (in->str[n] == ':')
      g_string_append (out, "\\:");
    else
      g_string_append_c (out, in->str[n]);
  }

  return out;
}

/* \\ = \, \c = : */
static GString *
unescape_string (const GString * in)
{
  GString * out;
  int n;

  out = g_string_sized_new (64);
  for (n = 0; n < (int) in->len; ++n) {
    if (in->str[n] == '\\') {
      if (n + 1 < (int) in->len) {
        ++n;
        if (in->str[n] == '\\')
          g_string_append_c (out, '\\');
        else if (in->str[n] == 'c')
          g_string_append_c (out, ':');
        else {
          /* unknown code, we will eat the escape sequence */
        }
      }
      else {
        /* string ends with backslash, we will eat it */
      }
    }
    else
      g_string_append_c (out, in->str[n]);
  }

  return out;
}

static gchar *
create_device_id (const gchar * sgname, int inputIndex)
{
  GString * out;
  GString * name;
  GString * nameenc;
  gchar * ret;

  name = g_string_new (sgname);
  nameenc = escape_string (name);
  g_string_free (name, TRUE);

  if (inputIndex >= 0) {
    out = g_string_new ("");
    g_string_printf (out, "%s:%d", nameenc->str, inputIndex);
  }
  else {
    /* unspecified index */
    out = g_string_new (nameenc->str);
  }

  g_string_free (nameenc, TRUE);
  ret = g_string_free (out, FALSE);
  return ret;
}

static gboolean
parse_device_id (const gchar * id, gchar ** sgname, int * inputIndex)
{
  gchar ** parts;
  int numparts;
  GString * p1;
  GString * out1;
  int out2;

  parts = g_strsplit (id, ":", -1);
  numparts = 0;
  while (parts[numparts])
    ++numparts;

  /* must be exactly 1 or 2 parts */
  if (numparts < 1 || numparts > 2) {
    g_strfreev (parts);
    return FALSE;
  }

  p1 = g_string_new (parts[0]);
  out1 = unescape_string (p1);
  g_string_free (p1, TRUE);

  if (numparts >= 2) {
    errno = 0;
    out2 = strtol (parts[1], NULL, 10);
    if (out2 == 0 && (errno == ERANGE || errno == EINVAL)) {
      g_string_free (out1, TRUE);
      g_strfreev (parts);
      return FALSE;
    }
  }

  g_strfreev (parts);

  *sgname = g_string_free (out1, FALSE);
  *inputIndex = out2;
  return TRUE;
}

typedef struct {
  gchar * id;
  gchar * name;
} video_device;

static video_device *
video_device_alloc ()
{
  video_device * dev;
  dev = g_malloc (sizeof (video_device));
  dev->id = NULL;
  dev->name = NULL;
  return dev;
}

static void
video_device_free (video_device * dev)
{
  if (!dev)
    return;

  if (dev->id)
    g_free (dev->id);
  if (dev->name)
    g_free (dev->name);

  g_free (dev);
}

static void
video_device_free_func (gpointer data, gpointer user_data)
{
  video_device_free ((video_device *) data);
}

/* return a list of available devices.  the default device (if any) will be
 * the first in the list.
 */
static GList *
device_list (GstOSXVideoSrc * src)
{
  SeqGrabComponent component;
  SGChannel channel;
  SGDeviceList deviceList;
  SGDeviceName * deviceEntry;
  SGDeviceInputList inputList;
  SGDeviceInputName * inputEntry;
  ComponentResult err;
  int n, i;
  GList * list;
  video_device * dev, * default_dev;
  gchar sgname[256];
  gchar friendly_name[256];

  list = NULL;

  if (!src->video_chan) {
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
  }
  else
    channel = src->video_chan;

  if (SGGetChannelDeviceList (channel, sgDeviceListIncludeInputs, &deviceList) != noErr) {
    CloseComponent (component);
    return FALSE;
  }

  for (n = 0; n < (*deviceList)->count; ++n) {
    deviceEntry = &(*deviceList)->entry[n];

    if (deviceEntry->flags & sgDeviceNameFlagDeviceUnavailable)
      continue;

    p2cstrcpy (sgname, deviceEntry->name);
    inputList = deviceEntry->inputs;

    if (inputList && (*inputList)->count >= 1) {
      for (i = 0; i < (*inputList)->count; ++i) {
        inputEntry = &(*inputList)->entry[i];

        p2cstrcpy (friendly_name, inputEntry->name);

        dev = video_device_alloc ();
        dev->id = create_device_id (sgname, i);
        if (!dev->id) {
          video_device_free (dev);
          i = -1;
          break;
        }

        dev->name = g_strdup (friendly_name);
        list = g_list_append (list, dev);

        /* if this is the default device, note it */
        if (n == (*deviceList)->selectedIndex && i == (*inputList)->selectedIndex) {
          default_dev = dev;
        }
      }

      /* error */
      if (i == -1)
        break;
    }
    else {
      /* ### can a device have no defined inputs? */
      dev = video_device_alloc ();
      dev->id = create_device_id (sgname, -1);
      if (!dev->id) {
        video_device_free (dev);
        break;
      }

      dev->name = g_strdup (sgname);
      list = g_list_append (list, dev);

      /* if this is the default device, note it */
      if (n == (*deviceList)->selectedIndex) {
        default_dev = dev;
      }
    }
  }

  /* move default device to the front */
  list = g_list_remove (list, default_dev);
  list = g_list_prepend (list, default_dev);

  if (!src->video_chan)
    CloseComponent (component);

  return list;
}

static gboolean
device_set_default (GstOSXVideoSrc * src)
{
  GList * list;
  video_device * dev;
  gboolean ret;

  /* obtain the device list */
  list = device_list (src);
  if (!list)
    return FALSE;

  ret = FALSE;

  /* the first item is the default */
  if (g_list_length (list) >= 1) {
    dev = (video_device *) list->data;

    /* take the strings, no need to copy */
    src->device_id = dev->id;
    src->device_name = dev->name;
    dev->id = NULL;
    dev->name = NULL;

    /* null out the item */
    video_device_free (dev);
    list->data = NULL;

    ret = TRUE;
  }

  /* clean up */
  g_list_foreach (list, video_device_free_func, NULL);
  g_list_free (list);

  return ret;
}

static gboolean
device_get_name (GstOSXVideoSrc * src)
{
  GList * l, * list;
  video_device * dev;
  gboolean ret;

  /* if there is no device set, then attempt to set up with the default,
   * which will also grab the name in the process.
   */
  if (!src->device_id)
    return device_set_default (src);

  /* if we already have a name, free it */
  if (src->device_name) {
    g_free (src->device_name);
    src->device_name = NULL;
  }

  /* obtain the device list */
  list = device_list (src);
  if (!list)
    return FALSE;

  ret = FALSE;

  /* look up the id */
  for (l = list; l != NULL; l = l->next) {
    dev = (video_device *) l->data;
    if (g_str_equal (dev->id, src->device_id)) {
      /* take the string, no need to copy */
      src->device_name = dev->name;
      dev->name = NULL;
      ret = TRUE;
      break;
    }
  }

  g_list_foreach (list, video_device_free_func, NULL);
  g_list_free (list);

  return ret;
}

static gboolean
device_select (GstOSXVideoSrc * src)
{
  Str63 pstr;
  ComponentResult err;
  gchar * sgname;
  int inputIndex;

  /* if there's no device id set, attempt to select default device */
  if (!src->device_id && !device_set_default (src))
    return FALSE;

  if (!parse_device_id (src->device_id, &sgname, &inputIndex))
    return FALSE;

  c2pstrcpy (pstr, sgname);
  g_free (sgname);

  err = SGSetChannelDevice (src->video_chan, (StringPtr) &pstr);
  if (err != noErr)
    return FALSE;

  err = SGSetChannelDeviceInput (src->video_chan, inputIndex);
  if (err != noErr)
    return FALSE;

  return TRUE;
}

static gboolean
gst_osx_video_src_iface_supported (GstImplementsInterface * iface, GType iface_type)
{
  return FALSE;
}

static void
gst_osx_video_src_interface_init (GstImplementsInterfaceClass * klass)
{
  /* default virtual functions */
  klass->supported = gst_osx_video_src_iface_supported;
}

static void
gst_osx_video_src_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_osx_video_src_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);

  gst_osx_video_src_type_add_device_property_probe_interface (type);
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
      if (src->device_id) {
        g_free (src->device_id);
        src->device_id = NULL;
      }
      if (src->device_name) {
        g_free (src->device_name);
        src->device_name = NULL;
      }
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

  if (!device_select (self))
    return FALSE;

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

static const GList *
probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass * klass = G_OBJECT_GET_CLASS (probe);
  static GList * list = NULL;

  // ###: from gstalsadeviceprobe.c
  /* well, not perfect, but better than no locking at all.
   * In the worst case we leak a list node, so who cares? */
  GST_CLASS_LOCK (GST_OBJECT_CLASS (klass));

  if (!list) {
    GParamSpec *pspec;

    pspec = g_object_class_find_property (klass, "device");
    list = g_list_append (NULL, pspec);
  }

  GST_CLASS_UNLOCK (GST_OBJECT_CLASS (klass));

  return list;
}

static void
probe_probe_property (GstPropertyProbe * probe, guint prop_id,
    const GParamSpec * pspec)
{
  // ###: i think this function is supposed to prepare the gvaluearray,
  //  but we can also just do nothing and have the array get prepared in the
  //  get_values() call instead.
  if (!g_str_equal (pspec->name, "device")) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
  }
}

static gboolean
probe_needs_probe (GstPropertyProbe * probe, guint prop_id,
    const GParamSpec * pspec)
{
  /* don't cache probed data */
  return TRUE;
}

static GValueArray *
probe_get_values (GstPropertyProbe * probe, guint prop_id,
    const GParamSpec * pspec)
{
  GstOSXVideoSrc * src;
  GValueArray * array;
  GValue value = { 0, };
  GList * l, * list;
  video_device * dev;

  if (!g_str_equal (pspec->name, "device")) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
    return NULL;
  }

  src = GST_OSX_VIDEO_SRC (probe);

  list = device_list (src);

  if (list == NULL) {
    GST_LOG_OBJECT (probe, "No devices found");
    return NULL;
  }

  array = g_value_array_new (g_list_length (list));
  g_value_init (&value, G_TYPE_STRING);
  for (l = list; l != NULL; l = l->next) {
    dev = (video_device *) l->data;
    GST_LOG_OBJECT (probe, "Found device: %s", dev->id);
    g_value_take_string (&value, dev->id);
    dev->id = NULL;
    video_device_free (dev);
    l->data = NULL;
    g_value_array_append (array, &value);
  }
  g_value_unset (&value);
  g_list_free (list);

  return array;
}

static void
gst_osx_video_src_property_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = probe_get_properties;
  iface->probe_property = probe_probe_property;
  iface->needs_probe = probe_needs_probe;
  iface->get_values = probe_get_values;
}

void
gst_osx_video_src_type_add_device_property_probe_interface (GType type)
{
  static const GInterfaceInfo probe_iface_info = {
    (GInterfaceInitFunc) gst_osx_video_src_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &probe_iface_info);
}
