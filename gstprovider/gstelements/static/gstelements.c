/*
 * Copyright (C) 2008  Barracuda Networks, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#include "gstelements.h"

#ifdef HAVE_RTPMANAGER
void gstelements_rtpmanager_register();
#endif

#ifdef HAVE_VIDEOMAXRATE
void gstelements_videomaxrate_register();
#endif

#ifdef HAVE_SPEEXDSP
void gstelements_speexdsp_register();
#endif

#ifdef HAVE_DIRECTSOUND
void gstelements_directsound_register();
#endif

#ifdef HAVE_WINKS
void gstelements_winks_register();
#endif

#ifdef HAVE_OSXAUDIO
void gstelements_osxaudio_register();
#endif

#ifdef HAVE_OSXVIDEO
void gstelements_osxvideo_register();
#endif

void gstelements_register()
{
#ifdef HAVE_RTPMANAGER
	gstelements_rtpmanager_register();
#endif

#ifdef HAVE_VIDEOMAXRATE
	gstelements_videomaxrate_register();
#endif

#ifdef HAVE_SPEEXDSP
	gstelements_speexdsp_register();
#endif

#ifdef HAVE_DIRECTSOUND
	gstelements_directsound_register();
#endif

#ifdef HAVE_WINKS
	gstelements_winks_register();
#endif

#ifdef HAVE_OSXAUDIO
	gstelements_osxaudio_register();
#endif

#ifdef HAVE_OSXVIDEO
	gstelements_osxvideo_register();
#endif
}
