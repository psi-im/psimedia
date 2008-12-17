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

#include <QtGlobal>

void gstelements_rtpmanager_register();
void gstelements_videomaxrate_register();
void gstelements_speexdsp_register();

#ifdef Q_OS_WIN
void gstelements_directsound_register();
void gstelements_winks_register();
#endif

#ifdef Q_OS_MAC
void gstelements_osxaudio_register();
void gstelements_osxvideo_register();
#endif

void gstelements_register()
{
	gstelements_rtpmanager_register();
	gstelements_videomaxrate_register();
	gstelements_speexdsp_register();

#ifdef Q_OS_WIN
	gstelements_directsound_register();
	gstelements_winks_register();
#endif

#ifdef Q_OS_MAC
	gstelements_osxaudio_register();
	gstelements_osxvideo_register();
#endif
}
