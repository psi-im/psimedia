/*
 * Copyright (C) 2008-2009  Barracuda Networks, Inc.
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

#include "pipeline.h"

#include <QList>
#include <QSet>
#include <gst/gst.h>

namespace PsiMedia {

class Pipeline
{
public:
	int refs;
	GstElement *pipeline;

	Pipeline() :
		refs(1)
	{
		pipeline = gst_pipeline_new(NULL);
		gst_element_set_state(pipeline, GST_STATE_PLAYING);
		gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
	}

	~Pipeline()
	{
		gst_element_set_state(pipeline, GST_STATE_NULL);
		gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
		gst_object_unref(GST_OBJECT(pipeline));
	}
};

static Pipeline *g_pipeline = 0;

GstElement *pipeline_global_ref()
{
	if(!g_pipeline)
		g_pipeline = new Pipeline;
	else
		++(g_pipeline->refs);

	return g_pipeline->pipeline;
}

void pipeline_global_unref()
{
	Q_ASSERT(g_pipeline);

	--(g_pipeline->refs);
	if(g_pipeline->refs == 0)
	{
		delete g_pipeline;
		g_pipeline = 0;
	}
}

class PipelineDevice
{
public:
	int refs;
	QString id;
	PDevice::Type type;
	GstElement *pipeline;
	GstElement *bin;

	// for srcs (audio or video)
	GstElement *tee;
	QSet<GstElement*> queues;

	// for sinks (audio only, video sinks are always unshared)
	GstElement *adder;

	PipelineDevice() :
		refs(1),
		tee(0),
		adder(0)
	{
		pipeline = pipeline_global_ref();
	}

	~PipelineDevice()
	{
		Q_ASSERT(queues.isEmpty());

		// TODO: safely remove bin, tee/adder from pipeline

		pipeline_global_unref();
	}
};

static QList<PipelineDevice*> *g_devices = 0;

GstElement *pipeline_device_ref(const QString &id, PDevice::Type type)
{
	if(!g_devices)
		g_devices = new QList<PipelineDevice*>();

	// see if we're already using this device, so we can attempt to share
	int at = -1;
	for(int n = 0; n < g_devices->count(); ++n)
	{
		PipelineDevice *dev = (*g_devices)[n];
		if(dev->id == id && dev->type == type)
		{
			at = n;
			break;
		}
	}

	// device is not in use, set it up
	if(at == -1)
	{
		PipelineDevice *dev = new PipelineDevice;
		dev->id = id;
		dev->type = type;

		dev->bin = 0; // TODO: create bin
		gst_bin_add(GST_BIN(dev->pipeline), dev->bin);

		if(dev->type == PDevice::AudioIn || dev->type == PDevice::VideoIn)
		{
			dev->tee = gst_element_factory_make("tee", NULL);
			gst_bin_add(GST_BIN(dev->pipeline), dev->tee);
			gst_element_link(dev->bin, dev->tee);
		}
		else // AudioOut
		{
			dev->adder = gst_element_factory_make("liveadder", NULL);
			gst_bin_add(GST_BIN(dev->pipeline), dev->adder);
			gst_element_link(dev->adder, dev->bin);
		}

		g_devices->append(dev);
		at = g_devices->count() - 1;
	}
	else
		++((*g_devices)[at]->refs);

	PipelineDevice *dev = (*g_devices)[at];

	if(dev->type == PDevice::AudioIn || dev->type == PDevice::VideoIn)
	{
		// create a queue from the tee, and hand it off.  app uses this queue
		//   element as if it were the actual device.
		GstElement *queue = gst_element_factory_make("queue", NULL);
		dev->queues += queue;
		gst_bin_add(GST_BIN(dev->pipeline), queue);
		gst_element_link(dev->tee, queue);
		return queue;
	}
	else // AudioOut
	{
		return dev->adder;
	}
}

void pipeline_device_unref(const QString &id, PDevice::Type type, GstElement *dev_elem)
{
	Q_ASSERT(g_devices);

	int at = -1;
	for(int n = 0; n < g_devices->count(); ++n)
	{
		PipelineDevice *dev = (*g_devices)[n];
		if(dev->id == id && dev->type == type)
		{
			if(dev->type == PDevice::AudioIn || dev->type == PDevice::VideoIn)
			{
				Q_ASSERT(dev->queues.contains(dev_elem));
			}
			else // AudioOut
			{
				Q_ASSERT(dev_elem == dev->adder);
			}

			at = n;
			break;
		}
	}

	Q_ASSERT(at != -1);

	PipelineDevice *dev = (*g_devices)[at];

	if(type == PDevice::AudioIn || type == PDevice::VideoIn)
	{
		GstElement *queue = dev_elem;
		gst_element_unlink(dev->tee, queue);
		gst_element_set_state(queue, GST_STATE_NULL);
		gst_element_get_state(queue, NULL, NULL, GST_CLOCK_TIME_NONE);
		gst_object_unref(GST_OBJECT(queue));
		dev->queues.remove(queue);
	}

	--dev->refs;
	if(dev->refs == 0)
	{
		g_devices->removeAt(at);
		delete dev;
	}
}

}
