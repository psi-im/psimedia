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
 * 
 */

#ifndef __GST_DIRECTSOUNDSINK_H__
#define __GST_DIRECTSOUNDSINK_H__

#include <gst/gst.h>
#include <gst/audio/gstbaseaudiosink.h>
#include <gst/audio/gstringbuffer.h>
#include <gst/interfaces/mixer.h>

#include <windows.h>
#include <dxerr9.h>

/* use directsound v8 */
#ifdef DIRECTSOUND_VERSION
  #undef DIRECTSOUND_VERSION
#endif

#define DIRECTSOUND_VERSION 0x0800

#include <dsound.h>

GST_DEBUG_CATEGORY_EXTERN (directsound_debug);

G_BEGIN_DECLS
#define GST_TYPE_DIRECTSOUND_SINK            (gst_directsound_sink_get_type())
#define GST_DIRECTSOUND_SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRECTSOUND_SINK,GstDirectSoundSink))
#define GST_DIRECTSOUND_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRECTSOUND_SINK,GstDirectSoundSinkClass))
#define GST_IS_DIRECTSOUND_SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRECTSOUND_SINK))
#define GST_IS_DIRECTSOUND_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRECTSOUND_SINK))

typedef struct _GstDirectSoundSink GstDirectSoundSink;
typedef struct _GstDirectSoundSinkClass GstDirectSoundSinkClass;

#define GST_DSOUND_LOCK(obj)	(g_mutex_lock (obj->dsound_lock))
#define GST_DSOUND_UNLOCK(obj)	(g_mutex_unlock (obj->dsound_lock))

#define GST_TYPE_DIRECTSOUND_RING_BUFFER            (gst_directsound_ring_buffer_get_type())
#define GST_DIRECTSOUND_RING_BUFFER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRECTSOUND_RING_BUFFER,GstDirectSoundRingBuffer))
#define GST_DIRECTSOUND_RING_BUFFER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRECTSOUND_RING_BUFFER,GstDirectSoundRingBufferClass))
#define GST_DIRECTSOUND_RING_BUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_DIRECTSOUND_RING_BUFFER,GstDirectSoundRingBufferClass))
#define GST_IS_DIRECTSOUND_RING_BUFFER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRECTSOUND_RING_BUFFER))
#define GST_IS_DIRECTSOUND_RING_BUFFER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRECTSOUND_RING_BUFFER))

typedef struct _GstDirectSoundRingBuffer GstDirectSoundRingBuffer;
typedef struct _GstDirectSoundRingBufferClass GstDirectSoundRingBufferClass;

/* 
 * DirectSound Sink
 */
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


/*
 * DirectSound Ring Buffer
 */
struct _GstDirectSoundRingBuffer {
	GstRingBuffer object;

  /* sink element */
  GstDirectSoundSink *dsoundsink;

  /* lock used to protect writes and resets */
  GMutex *dsound_lock;

  /* directsound buffer waveformat description */
  WAVEFORMATEX wave_format;

  /* directsound object interface pointer */
  LPDIRECTSOUND8 pDS8;

  /* directsound sound object interface pointer */
  LPDIRECTSOUNDBUFFER8 pDSB8;

  /* directsound buffer size */
  guint buffer_size;

  /* directsound buffer write offset */
  guint buffer_write_offset;

  /* minimum buffer size before playback start */
  guint min_buffer_size;

  /* minimum sleep time for thread */
  guint min_sleep_time;

  /* ringbuffer bytes per sample */
  guint bytes_per_sample;

  /* ringbuffer segment size */
  gint segsize;

  /* ring buffer offset*/
  guint segoffset;

  /* thread */
  HANDLE hThread;

  /* thread suspended? */
  gboolean suspended;

  /* should run thread */
  gboolean should_run;

  /* are we currently flushing? */
  gboolean flushing;

  /* current volume */
  gdouble volume;
};

struct _GstDirectSoundRingBufferClass {
	GstRingBufferClass    parent_class;
};

GType gst_directsound_ring_buffer_get_type (void);

G_END_DECLS
#endif /* __GST_DIRECTSOUNDSINK_H__ */
