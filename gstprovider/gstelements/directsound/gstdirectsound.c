/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007-2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * gstdirectsound.c:
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

// note: INITGUID clashes with amstrmid.lib from winks, if both plugins are
//   statically built together.  we'll get around this by only defining the
//   directsound-specific GUIDs we actually use (see dsguids.c)
//#define INITGUID

#include "gstdirectsound.h"

#include <math.h>

GST_DEBUG_CATEGORY (directsound);

#define GST_CAT_DEFAULT directsound

gst_directsound_device *
gst_directsound_device_alloc ()
{
  gst_directsound_device * dev;
  dev = g_malloc (sizeof (gst_directsound_device));
  dev->id = NULL;
  dev->name = NULL;
  return dev;
}

void
gst_directsound_device_free (gst_directsound_device * dev)
{
  if (!dev)
    return;

  if (dev->id)
    g_free (dev->id);
  if (dev->name)
    g_free (dev->name);

  g_free (dev);
}

void
gst_directsound_device_free_func (gpointer data, gpointer user_data)
{
  gst_directsound_device_free ((gst_directsound_device *) data);
}

GList *
gst_directsound_playback_device_list ()
{
  // TODO
  return NULL;
}

GList *
gst_directsound_capture_device_list ()
{
  // TODO
  return NULL;
}

void
gst_directsound_device_list_free (GList * list)
{
  g_list_foreach (list, gst_directsound_device_free_func, NULL);
  g_list_free (list);
}

LPGUID
gst_directsound_get_device_guid (const gchar * id)
{
  // TODO
  return NULL;
}

void
gst_directsound_set_volume (LPDIRECTSOUNDBUFFER8 pDSB8, gdouble volume)
{
  HRESULT hr;
  long dsVolume;

  /* DirectSound controls volume using units of 100th of a decibel,
   * ranging from -10000 to 0. We use a linear scale of 0 - 100
   * here, so remap.
   */
  if (volume == 0)
    dsVolume = -10000;
  else
    dsVolume = 100 * (long) (20 * log10 (volume));

  dsVolume = CLAMP (dsVolume, -10000, 0);

  GST_DEBUG ("Setting volume on secondary buffer to %d", (int) dsVolume);

  hr = IDirectSoundBuffer8_SetVolume (pDSB8, dsVolume);
  if (G_UNLIKELY (FAILED(hr))) {
    GST_WARNING ("Setting volume on secondary buffer failed.");
  }
}
