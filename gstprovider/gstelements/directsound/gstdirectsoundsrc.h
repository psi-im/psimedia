/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007-2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 * Copyright (C) 2009 Barracuda Networks, Inc.
 *
 * gstdirectsoundsrc.h:
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

#ifndef __GST_DIRECTSOUNDSRC_H__
#define __GST_DIRECTSOUNDSRC_H__

#include <gst/gst.h>
#include <gst/audio/gstbaseaudiosrc.h>
#include "gstdirectsound.h"
#include "gstdirectsoundringbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_DIRECTSOUND_SRC \
    (gst_directsound_src_get_type())
#define GST_DIRECTSOUND_SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRECTSOUND_SRC,GstDirectSoundSrc))
#define GST_DIRECTSOUND_SRC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRECTSOUND_SRC,GstDirectSoundSrcClass))
#define GST_IS_DIRECTSOUND_SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRECTSOUND_SRC))
#define GST_IS_DIRECTSOUND_SRC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRECTSOUND_SRC))

typedef struct _GstDirectSoundSrc GstDirectSoundSrc;
typedef struct _GstDirectSoundSrcClass GstDirectSoundSrcClass;

struct _GstDirectSoundSrc
{
  /* base audio src */
  GstBaseAudioSrc src;

  /* ringbuffer */
  GstDirectSoundRingBuffer * dsoundbuffer;

  /* current volume */
  gdouble volume;
};

struct _GstDirectSoundSrcClass
{
  GstBaseAudioSrcClass parent_class;
};

GType gst_directsound_src_get_type (void);

G_END_DECLS

#endif /* __GST_DIRECTSOUNDSRC_H__ */
