/*
 * Copyright (C) 2009  Barracuda Networks, Inc.
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

#ifndef PSI_PIPELINE_H
#define PSI_PIPELINE_H

#include <QString>
#include <gst/gstelement.h>
#include "psimediaprovider.h"

namespace PsiMedia {

GstElement *pipeline_global_ref();
void pipeline_global_unref();

class PipelineDeviceOptions
{
public:
	QSize videoSize;
	int fps;

	PipelineDeviceOptions() :
		fps(-1)
	{
	}
};

GstElement *pipeline_device_ref(const QString &id, PDevice::Type type, const PipelineDeviceOptions &opts = PipelineDeviceOptions());
void pipeline_device_set_opts(GstElement *dev_elem, const PipelineDeviceOptions &opts);
void pipeline_device_unref(GstElement *dev_elem);

}

#endif
