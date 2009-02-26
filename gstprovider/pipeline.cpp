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

#include "pipeline.h"

#include <QList>
#include <QSet>
#include <gst/gst.h>
#include "devices.h"

namespace PsiMedia {

static const char *type_to_str(PDevice::Type type)
{
	switch(type)
	{
		case PDevice::AudioIn:  return "AudioIn";
		case PDevice::AudioOut: return "AudioOut";
		case PDevice::VideoIn:  return "VideoIn";
		default:
			Q_ASSERT(0);
			return 0;
	}
}

static void videosrcbin_pad_added(GstElement *element, GstPad *pad, gpointer data)
{
	Q_UNUSED(element);
	GstPad *gpad = (GstPad *)data;

	gchar *name = gst_pad_get_name(pad);
	//printf("videosrcbin pad-added: %s\n", name);
	g_free(name);

	GstCaps *caps = gst_pad_get_caps(pad);
	gchar *gstr = gst_caps_to_string(caps);
	QString capsString = QString::fromUtf8(gstr);
	g_free(gstr);
	//printf("  caps: [%s]\n", qPrintable(capsString));

	gst_ghost_pad_set_target(GST_GHOST_PAD(gpad), pad);

	gst_caps_unref(caps);
}

static GstStaticPadTemplate videosrcbin_template = GST_STATIC_PAD_TEMPLATE("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw-yuv; "
		"video/x-raw-rgb"
		)
	);

static GstElement *filter_for_capture_size(const QSize &size)
{
	GstElement *capsfilter = gst_element_factory_make("capsfilter", NULL);
	GstCaps *caps = gst_caps_new_empty();

	GstStructure *cs;
	cs = gst_structure_new("video/x-raw-yuv",
		"width", G_TYPE_INT, size.width(),
		"height", G_TYPE_INT, size.height(), NULL);
	gst_caps_append_structure(caps, cs);

	cs = gst_structure_new("video/x-raw-rgb",
		"width", G_TYPE_INT, size.width(),
		"height", G_TYPE_INT, size.height(), NULL);
	gst_caps_append_structure(caps, cs);

	cs = gst_structure_new("image/jpeg",
		"width", G_TYPE_INT, size.width(),
		"height", G_TYPE_INT, size.height(), NULL);
	gst_caps_append_structure(caps, cs);

	g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
	gst_caps_unref(caps);

	return capsfilter;
}

static GstElement *filter_for_desired_size(const QSize &size)
{
	QList<int> widths;
	widths << 160 << 320 << 640 << 800 << 1024;
	for(int n = 0; n < widths.count(); ++n)
	{
		if(widths[n] < size.width())
		{
			widths.removeAt(n);
			--n; // adjust position
		}
	}

	GstElement *capsfilter = gst_element_factory_make("capsfilter", NULL);
	GstCaps *caps = gst_caps_new_empty();

	for(int n = 0; n < widths.count(); ++n)
	{
		GstStructure *cs;
		cs = gst_structure_new("video/x-raw-yuv",
			"width", GST_TYPE_INT_RANGE, 1, widths[n],
			"height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
		gst_caps_append_structure(caps, cs);

		cs = gst_structure_new("video/x-raw-rgb",
			"width", GST_TYPE_INT_RANGE, 1, widths[n],
			"height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
		gst_caps_append_structure(caps, cs);
	}

	GstStructure *cs = gst_structure_new("image/jpeg", NULL);
	gst_caps_append_structure(caps, cs);

	g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
	gst_caps_unref(caps);

	return capsfilter;
}

static GstElement *make_devicebin(const QString &id, PDevice::Type type, const QSize &desiredSize)
{
	QSize captureSize;
	GstElement *e = devices_makeElement(id, type, &captureSize);
	if(!e)
		return 0;

	if(type == PDevice::AudioIn)
		return e;

	GstElement *bin = gst_bin_new(NULL);

	if(type == PDevice::VideoIn)
	{
		GstElement *capsfilter = 0;
		if(captureSize.isValid())
			capsfilter = filter_for_capture_size(captureSize);
		else if(desiredSize.isValid())
			capsfilter = filter_for_desired_size(desiredSize);

		gst_bin_add(GST_BIN(bin), e);

		if(capsfilter)
			gst_bin_add(GST_BIN(bin), capsfilter);

		GstElement *decodebin = gst_element_factory_make("decodebin", NULL);
		gst_bin_add(GST_BIN(bin), decodebin);

		GstPad *pad = gst_ghost_pad_new_no_target_from_template("src",
			gst_static_pad_template_get(&videosrcbin_template));
		gst_element_add_pad(bin, pad);

		g_signal_connect(G_OBJECT(decodebin),
			"pad-added",
			G_CALLBACK(videosrcbin_pad_added), pad);

		if(capsfilter)
			gst_element_link_many(e, capsfilter, decodebin, NULL);
		else
			gst_element_link(e, decodebin);
	}
	else // AudioOut
	{
		GstElement *audioconvert = gst_element_factory_make("audioconvert", NULL);
		GstElement *audioresample = gst_element_factory_make("audioresample", NULL);
		gst_bin_add(GST_BIN(bin), audioconvert);
		gst_bin_add(GST_BIN(bin), audioresample);
		gst_bin_add(GST_BIN(bin), e);

		gst_element_link_many(audioconvert, audioresample, e, NULL);

		GstPad *pad = gst_element_get_static_pad(audioconvert, "sink");
		gst_element_add_pad(bin, gst_ghost_pad_new("sink", pad));
		gst_object_unref(GST_OBJECT(pad));
	}

	return bin;
}

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
		g_object_unref(G_OBJECT(pipeline));
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

		if(adder)
			gst_element_set_state(adder, GST_STATE_NULL);

		gst_element_set_state(bin, GST_STATE_NULL);

		if(tee)
			gst_element_set_state(tee, GST_STATE_NULL);

		if(adder)
		{
			gst_element_get_state(adder, NULL, NULL, GST_CLOCK_TIME_NONE);
			gst_bin_remove(GST_BIN(pipeline), adder);
		}

		gst_element_get_state(bin, NULL, NULL, GST_CLOCK_TIME_NONE);
		gst_bin_remove(GST_BIN(pipeline), bin);

		if(tee)
		{
			gst_element_get_state(tee, NULL, NULL, GST_CLOCK_TIME_NONE);
			gst_bin_remove(GST_BIN(pipeline), tee);
		}

		pipeline_global_unref();
	}
};

static QList<PipelineDevice*> *g_devices = 0;

GstElement *pipeline_device_ref(const QString &id, PDevice::Type type, const PipelineDeviceOptions &opts)
{
	// TODO: use opts.fps?

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

		dev->bin = make_devicebin(id, type, opts.videoSize);
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

		if(dev->adder)
			gst_element_set_state(dev->adder, GST_STATE_PLAYING);

		gst_element_set_state(dev->bin, GST_STATE_PLAYING);

		if(dev->tee)
			gst_element_set_state(dev->tee, GST_STATE_PLAYING);

		// srcs will change state immediately
		if(dev->type == PDevice::AudioIn || dev->type == PDevice::VideoIn)
		{
			gst_element_get_state(dev->bin, NULL, NULL, GST_CLOCK_TIME_NONE);
			gst_element_get_state(dev->tee, NULL, NULL, GST_CLOCK_TIME_NONE);
		}

		g_devices->append(dev);
		at = g_devices->count() - 1;
	}
	else
		++((*g_devices)[at]->refs);

	PipelineDevice *dev = (*g_devices)[at];
	printf("Readying %s:[%s], refs=%d\n", type_to_str(dev->type), qPrintable(dev->id), dev->refs);

	if(dev->type == PDevice::AudioIn || dev->type == PDevice::VideoIn)
	{
		// create a queue from the tee, and hand it off.  app uses this queue
		//   element as if it were the actual device.
		GstElement *queue = gst_element_factory_make("queue", NULL);
		dev->queues += queue;
		gst_bin_add(GST_BIN(dev->pipeline), queue);
		gst_element_set_state(queue, GST_STATE_PLAYING);
		gst_element_get_state(queue, NULL, NULL, GST_CLOCK_TIME_NONE);
		gst_element_link(dev->tee, queue);
		return queue;
	}
	else // AudioOut
	{
		return dev->adder;
	}
}

void pipeline_device_set_opts(GstElement *dev_elem, const PipelineDeviceOptions &opts)
{
	// TODO
	Q_UNUSED(dev_elem);
	Q_UNUSED(opts);
}

void pipeline_device_unref(GstElement *dev_elem)
{
	Q_ASSERT(g_devices);

	int at = -1;
	for(int n = 0; n < g_devices->count(); ++n)
	{
		PipelineDevice *dev = (*g_devices)[n];
		if(dev->type == PDevice::AudioIn || dev->type == PDevice::VideoIn)
		{
			if(dev->queues.contains(dev_elem))
			{
				at = n;
				break;
			}
		}
		else // AudioOut
		{
			if(dev_elem == dev->adder)
			{
				at = n;
				break;
			}
		}
	}

	Q_ASSERT(at != -1);

	PipelineDevice *dev = (*g_devices)[at];

	if(dev->type == PDevice::AudioIn || dev->type == PDevice::VideoIn)
	{
		GstElement *queue = dev_elem;

		// get tee and prepare srcpad
		GstPad *sinkpad = gst_element_get_pad(queue, "sink");
		GstPad *srcpad = gst_pad_get_peer(sinkpad);
		gst_object_unref(GST_OBJECT(sinkpad));
		gst_element_release_request_pad(dev->tee, srcpad);
		gst_object_unref(GST_OBJECT(srcpad));

		// safely remove queue
		gst_element_set_state(queue, GST_STATE_NULL);
		gst_element_get_state(queue, NULL, NULL, GST_CLOCK_TIME_NONE);
		gst_bin_remove(GST_BIN(dev->pipeline), queue);
		dev->queues.remove(queue);
	}

	--dev->refs;
	printf("Releasing %s:[%s], refs=%d\n", type_to_str(dev->type), qPrintable(dev->id), dev->refs);
	if(dev->refs == 0)
	{
		g_devices->removeAt(at);
		delete dev;
	}
}

}
