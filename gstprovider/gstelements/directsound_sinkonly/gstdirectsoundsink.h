/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007-2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * gstdirectsoundsink.h:
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

#ifndef __GST_DIRECTSOUNDSINK_H__
#define __GST_DIRECTSOUNDSINK_H__

#include <gst/gst.h>
#include <gst/audio/gstbaseaudiosink.h>
#include "gstdirectsound.h"
#include "gstdirectsoundringbuffer.h"

GST_DEBUG_CATEGORY_EXTERN (directsound);

G_BEGIN_DECLS

#define GST_TYPE_DIRECTSOUND_SINK            (gst_directsound_sink_get_type())
#define GST_DIRECTSOUND_SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRECTSOUND_SINK,GstDirectSoundSink))
#define GST_DIRECTSOUND_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRECTSOUND_SINK,GstDirectSoundSinkClass))
#define GST_IS_DIRECTSOUND_SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRECTSOUND_SINK))
#define GST_IS_DIRECTSOUND_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRECTSOUND_SINK))

typedef struct _GstDirectSoundSink GstDirectSoundSink;
typedef struct _GstDirectSoundSinkClass GstDirectSoundSinkClass;

struct _GstDirectSoundSink
{
  /* base audio sink */
  GstBaseAudioSink sink;

  /* ringbuffer */
  GstDirectSoundRingBuffer *dsoundbuffer;

  /* current volume */
  gdouble volume;
};

struct _GstDirectSoundSinkClass
{
  GstBaseAudioSinkClass parent_class;
};

GType gst_directsound_sink_get_type (void);

G_END_DECLS

#endif /* __GST_DIRECTSOUNDSINK_H__ */
