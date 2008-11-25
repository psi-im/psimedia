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

#include "modes.h"

namespace PsiMedia {

// FIXME: any better way besides hardcoding?

QList<PAudioParams> modes_supportedAudio()
{
	QList<PAudioParams> list;
	{
		PAudioParams p;
		p.codec = "pcmu";
		p.sampleRate = 8000;
		p.sampleSize = 16;
		p.channels = 1;
		list += p;
	}
	{
		PAudioParams p;
		p.codec = "speex";
		p.sampleRate = 8000;
		p.sampleSize = 16;
		p.channels = 1;
		list += p;
	}
	{
		PAudioParams p;
		p.codec = "speex";
		p.sampleRate = 16000;
		p.sampleSize = 16;
		p.channels = 1;
		list += p;
	}
	{
		PAudioParams p;
		p.codec = "speex";
		p.sampleRate = 32000;
		p.sampleSize = 16;
		p.channels = 1;
		list += p;
	}
	{
		PAudioParams p;
		p.codec = "vorbis";
		p.sampleRate = 44100;
		p.sampleSize = 16;
		p.channels = 2;
		list += p;
	}
	return list;
}

QList<PVideoParams> modes_supportedVideo()
{
	QList<PVideoParams> list;
	{
		PVideoParams p;
		p.codec = "h263p";
		p.size = QSize(160, 120);
		p.fps = 15;
		list += p;
	}
	{
		PVideoParams p;
		p.codec = "theora";
		p.size = QSize(160, 120);
		p.fps = 15;
		list += p;
	}
	{
		PVideoParams p;
		p.codec = "theora";
		p.size = QSize(320, 240);
		p.fps = 15;
		list += p;
	}
	{
		PVideoParams p;
		p.codec = "theora";
		p.size = QSize(320, 240);
		p.fps = 30;
		list += p;
	}
	{
		PVideoParams p;
		p.codec = "theora";
		p.size = QSize(640, 480);
		p.fps = 15;
		list += p;
	}
	{
		PVideoParams p;
		p.codec = "theora";
		p.size = QSize(640, 480);
		p.fps = 30;
		list += p;
	}
	return list;
}

}
