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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * The development of this code was made possible due to the involvement
 * of Pioneers of the Inevitable, the creators of the Songbird Music player
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define INITGUID

#include "gstdirectsound.h"

#include <math.h>

GST_DEBUG_CATEGORY(directsound);

#define GST_CAT_DEFAULT directsound

void gst_directsound_set_volume(LPDIRECTSOUNDBUFFER8 pDSB8, gdouble volume)
{
    HRESULT hr;
    long    dsVolume;

    /* DirectSound controls volume using units of 100th of a decibel,
     * ranging from -10000 to 0. We use a linear scale of 0 - 100
     * here, so remap.
     */
    if (volume == 0)
        dsVolume = -10000;
    else
        dsVolume = 100 * (long)(20 * log10(volume));

    dsVolume = CLAMP(dsVolume, -10000, 0);

    GST_DEBUG("Setting volume on secondary buffer to %d", (int)dsVolume);

    hr = IDirectSoundBuffer8_SetVolume(pDSB8, dsVolume);
    if (G_UNLIKELY(FAILED(hr))) {
        GST_WARNING("Setting volume on secondary buffer failed.");
    }
}
