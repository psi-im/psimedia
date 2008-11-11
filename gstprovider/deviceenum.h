/*
 * Copyright (C) 2006  Justin Karneges
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef DEVICEENUM_H
#define DEVICEENUM_H

#include <QtCore>

namespace DeviceEnum {

class Item
{
public:
	enum Type
	{
		Audio,
		Video
	};

	enum Direction
	{
		Input,
		Output
	};

	Type type;      // Audio or Video
	Direction dir;  // Input (mic) or Output (speaker)
	QString name;   // friendly name
	QString driver; // e.g. "oss", "alsa"
	QString id;     // e.g. "/dev/dsp", "hw:0,0"
};

// Note:
//  There will almost always be duplicate devices returned by
//  the following functions.  It is up to the user of this interface
//  to filter the results as necessary.  Possible duplications:
//    - ALSA devices showing up twice (hw and plughw)
//    - Video4Linux devices showing up twice (V4L and V4L2)
//    - Both OSS and ALSA entries showing up for the same device

QList<Item> audioOutputItems(const QString &driver = QString());
QList<Item> audioInputItems(const QString &driver = QString());
QList<Item> videoInputItems(const QString &driver = QString());

}

#endif
