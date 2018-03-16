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

#ifndef DEVICES_H
#define DEVICES_H

#include <QString>
#include <QList>
#include <gst/gstelement.h>
#include "psimediaprovider.h"

class QSize;

namespace PsiMedia {

class GstDevice
{
public:
    PDevice::Type type;
	QString name;
	bool isDefault;
	QString id;
};

QList<GstDevice> devices_list(PDevice::Type type);
GstElement *devices_makeElement(const QString &id, PDevice::Type type, QSize *captureSize = 0);

}

#endif
