/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007-2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * gstdirectsound.h:
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

#ifndef __GST_DIRECTSOUND_H__
#define __GST_DIRECTSOUND_H__

#include <gst/gst.h>

#include <windows.h>
#include <dxerr9.h>

/* use directsound v8 */
#ifdef DIRECTSOUND_VERSION
  #undef DIRECTSOUND_VERSION
#endif

#define DIRECTSOUND_VERSION 0x0800

#include <dsound.h>

GST_DEBUG_CATEGORY_EXTERN (directsound);

G_BEGIN_DECLS

void directsound_set_volume (LPDIRECTSOUNDBUFFER8 pDSB8, gdouble volume);

G_END_DECLS

#endif /* __GST_DIRECTSOUND_H__ */
