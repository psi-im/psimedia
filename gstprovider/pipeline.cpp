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

#define PIPELINE_DEBUG

// FIXME: this file is heavily commented out and a mess, mainly because
//   all of my attempts at a dynamic pipeline were futile.  someday we
//   can uncomment and clean this up...

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

	//gchar *name = gst_pad_get_name(pad);
	//printf("videosrcbin pad-added: %s\n", name);
	//g_free(name);

	GstCaps *caps = gst_pad_get_caps(pad);
	//gchar *gstr = gst_caps_to_string(caps);
	//QString capsString = QString::fromUtf8(gstr);
	//g_free(gstr);
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

	GstElement *bin = gst_bin_new(NULL);

	if(type == PDevice::AudioIn)
	{
		gst_bin_add(GST_BIN(bin), e);

		GstElement *audioconvert = gst_element_factory_make("audioconvert", NULL);
		GstElement *audioresample = gst_element_factory_make("audioresample", NULL);

		GstElement *capsfilter = gst_element_factory_make("capsfilter", NULL);
		GstCaps *caps = gst_caps_new_empty();
		GstStructure *cs = gst_structure_new("audio/x-raw-int",
			"rate", G_TYPE_INT, 22050,
			"width", G_TYPE_INT, 16,
			"channels", G_TYPE_INT, 1, NULL);
		gst_caps_append_structure(caps, cs);
		g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
		gst_caps_unref(caps);

		gst_bin_add(GST_BIN(bin), audioconvert);
		gst_bin_add(GST_BIN(bin), audioresample);
		gst_bin_add(GST_BIN(bin), capsfilter);

		gst_element_link_many(e, audioconvert, audioresample, capsfilter, NULL);

		GstPad *pad = gst_element_get_static_pad(capsfilter, "src");
		gst_element_add_pad(bin, gst_ghost_pad_new("src", pad));
		gst_object_unref(GST_OBJECT(pad));
	}
	else if(type == PDevice::VideoIn)
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

//----------------------------------------------------------------------------
// PipelineContext
//----------------------------------------------------------------------------
static GstElement *g_speexdsp = 0;
static GstElement *g_speexprobe = 0;

class PipelineDevice;

class PipelineDeviceContextPrivate
{
public:
	PipelineContext *pipeline;
	PipelineDevice *device;
	PipelineDeviceOptions opts;
	bool activated;

	// queue for srcs, adder for sinks
	GstElement *element;
};

class PipelineDevice
{
public:
	int refs;
	QString id;
	PDevice::Type type;
	GstElement *pipeline;
	GstElement *bin;
	bool activated;

	QSet<PipelineDeviceContextPrivate*> contexts;

	// for srcs
	GstElement *speexdsp;
	GstElement *tee;

	// for sinks (audio only, video sinks are always unshared)
	GstElement *adder;
	GstElement *audioconvert;
	GstElement *audioresample;
	GstElement *capsfilter;
	GstElement *speexprobe;

	PipelineDevice(const QString &_id, PDevice::Type _type, PipelineDeviceContextPrivate *context) :
		refs(0),
		id(_id),
		type(_type),
		activated(false),
		speexdsp(0),
		tee(0),
		adder(0),
		speexprobe(0)
	{
		pipeline = context->pipeline->element();

		bin = make_devicebin(id, type, context->opts.videoSize);
		if(!bin)
			return;

		// TODO: use context->opts.fps?

		if(type == PDevice::AudioIn || type == PDevice::VideoIn)
		{
			if(!g_speexdsp)
			{
				speexdsp = gst_element_factory_make("speexdsp", NULL);
				if(speexdsp)
				{
#ifdef PIPELINE_DEBUG
					printf("using speexdsp\n");
#endif
					g_speexdsp = speexdsp;
				}
			}

			if(speexdsp)
			{
				//gst_element_set_locked_state(speexdsp, TRUE);
				gst_bin_add(GST_BIN(pipeline), speexdsp);
			}

			tee = gst_element_factory_make("tee", NULL);
			//gst_element_set_locked_state(tee, TRUE);
			gst_bin_add(GST_BIN(pipeline), tee);

			//gst_element_set_locked_state(bin, TRUE);
			gst_bin_add(GST_BIN(pipeline), bin);

			if(speexdsp)
				gst_element_link_many(bin, speexdsp, tee, NULL);
			else
				gst_element_link(bin, tee);
		}
		else // AudioOut
		{
			adder = gst_element_factory_make("liveadder", NULL);

			audioconvert = gst_element_factory_make("audioconvert", NULL);
			audioresample = gst_element_factory_make("audioresample", NULL);

			capsfilter = gst_element_factory_make("capsfilter", NULL);
			GstCaps *caps = gst_caps_new_empty();
			GstStructure *cs = gst_structure_new("audio/x-raw-int",
				"rate", G_TYPE_INT, 22050,
				"width", G_TYPE_INT, 16,
				"channels", G_TYPE_INT, 1, NULL);
			gst_caps_append_structure(caps, cs);
			g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
			gst_caps_unref(caps);

			if(!g_speexprobe)
			{
				speexprobe = gst_element_factory_make("speexechoprobe", NULL);
				QString latency_tune = qgetenv("PSI_AUDIO_LTUNE");
				if(!latency_tune.isEmpty())
					g_object_set(G_OBJECT(speexprobe), "latency-tune", latency_tune.toInt(), NULL);
				if(speexprobe)
					g_speexprobe = speexprobe;
			}

			gst_bin_add(GST_BIN(pipeline), bin);
			gst_bin_add(GST_BIN(pipeline), adder);
			gst_bin_add(GST_BIN(pipeline), audioconvert);
			gst_bin_add(GST_BIN(pipeline), audioresample);
			gst_bin_add(GST_BIN(pipeline), capsfilter);

			if(speexprobe)
				gst_bin_add(GST_BIN(pipeline), speexprobe);

			gst_element_link_many(adder, audioconvert, audioresample, capsfilter, NULL);

			if(speexprobe)
				gst_element_link_many(capsfilter, speexprobe, bin, NULL);
			else
				gst_element_link(capsfilter, bin);

			/*gst_element_set_state(bin, GST_STATE_PLAYING);
			if(speexprobe)
				gst_element_set_state(speexprobe, GST_STATE_PLAYING);
			gst_element_set_state(capsfilter, GST_STATE_PLAYING);
			gst_element_set_state(audioresample, GST_STATE_PLAYING);
			gst_element_set_state(audioconvert, GST_STATE_PLAYING);
			gst_element_set_state(adder, GST_STATE_PLAYING);*/

			// sink starts out activated
			activated = true;
		}

		addRef(context);
	}

	~PipelineDevice()
	{
		Q_ASSERT(contexts.isEmpty());

		if(!bin)
			return;

		if(type == PDevice::AudioIn || type == PDevice::VideoIn)
		{
			gst_bin_remove(GST_BIN(pipeline), bin);

			if(speexdsp)
			{
				gst_bin_remove(GST_BIN(pipeline), speexdsp);
				g_speexdsp = 0;
			}

			if(tee)
				gst_bin_remove(GST_BIN(pipeline), tee);
		}
		else // AudioOut
		{
			if(adder)
			{
				gst_element_set_state(adder, GST_STATE_NULL);
				gst_element_set_state(audioconvert, GST_STATE_NULL);
				gst_element_set_state(audioresample, GST_STATE_NULL);
				gst_element_set_state(capsfilter, GST_STATE_NULL);
				if(speexprobe)
					gst_element_set_state(speexprobe, GST_STATE_NULL);
			}

			gst_element_set_state(bin, GST_STATE_NULL);

			if(adder)
			{
				gst_element_get_state(adder, NULL, NULL, GST_CLOCK_TIME_NONE);
				gst_bin_remove(GST_BIN(pipeline), adder);

				gst_element_get_state(audioconvert, NULL, NULL, GST_CLOCK_TIME_NONE);
				gst_bin_remove(GST_BIN(pipeline), audioconvert);

				gst_element_get_state(audioresample, NULL, NULL, GST_CLOCK_TIME_NONE);
				gst_bin_remove(GST_BIN(pipeline), audioresample);

				gst_element_get_state(capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
				gst_bin_remove(GST_BIN(pipeline), capsfilter);

				if(speexprobe)
				{
					gst_element_get_state(speexprobe, NULL, NULL, GST_CLOCK_TIME_NONE);
					gst_bin_remove(GST_BIN(pipeline), speexprobe);
					g_speexprobe = 0;
				}
			}

			gst_bin_remove(GST_BIN(pipeline), bin);
		}
	}

	void addRef(PipelineDeviceContextPrivate *context)
	{
		Q_ASSERT(!contexts.contains(context));

		// TODO: consider context->opts for refs after first

		if(type == PDevice::AudioIn || type == PDevice::VideoIn)
		{
			// create a queue from the tee, and hand it off.  app
			//   uses this queue element as if it were the actual
			//   device
			GstElement *queue = gst_element_factory_make("queue", NULL);
			context->element = queue;
			//gst_element_set_locked_state(queue, TRUE);
			gst_bin_add(GST_BIN(pipeline), queue);
			gst_element_link(tee, queue);
		}
		else // AudioOut
		{
			context->element = adder;

			// sink starts out activated
			context->activated = true;
		}

		contexts += context;
		++refs;
	}

	void removeRef(PipelineDeviceContextPrivate *context)
	{
		Q_ASSERT(contexts.contains(context));

		// TODO: recalc video properties

		if(type == PDevice::AudioIn || type == PDevice::VideoIn)
		{
			// deactivate if not done so already
			deactivate(context);

			GstElement *queue = context->element;
			gst_bin_remove(GST_BIN(pipeline), queue);
		}

		contexts.remove(context);
		--refs;
	}

	void activate(PipelineDeviceContextPrivate *context)
	{
		// activate the context
		if(!context->activated)
		{
			//GstElement *queue = context->element;
			//gst_element_set_locked_state(queue, FALSE);
			//gst_element_set_state(queue, GST_STATE_PLAYING);
			context->activated = true;
		}

		// activate the device
		if(!activated)
		{
			//gst_element_set_locked_state(tee, FALSE);
			//gst_element_set_locked_state(bin, FALSE);
			//gst_element_set_state(tee, GST_STATE_PLAYING);
			//gst_element_set_state(bin, GST_STATE_PLAYING);
			activated = true;
		}
	}

	void deactivate(PipelineDeviceContextPrivate *context)
	{
#if 0
		if(activated && refs == 1)
		{
 			if(type == PDevice::AudioIn || type == PDevice::VideoIn)
			{
				gst_element_set_locked_state(bin, TRUE);

				if(speexdsp)
					gst_element_set_locked_state(speexdsp, TRUE);

				if(tee)
					gst_element_set_locked_state(tee, TRUE);
			}
		}

		if(context->activated)
		{
 			if(type == PDevice::AudioIn || type == PDevice::VideoIn)
			{
				GstElement *queue = context->element;
				gst_element_set_locked_state(queue, TRUE);
			}
		}

		if(activated && refs == 1)
		{
 			if(type == PDevice::AudioIn || type == PDevice::VideoIn)
			{
				gst_element_set_state(bin, GST_STATE_NULL);
				gst_element_get_state(bin, NULL, NULL, GST_CLOCK_TIME_NONE);

				//printf("set to null\n");
				if(speexdsp)
				{
					gst_element_set_state(speexdsp, GST_STATE_NULL);
					gst_element_get_state(speexdsp, NULL, NULL, GST_CLOCK_TIME_NONE);
				}

				if(tee)
				{
					gst_element_set_state(tee, GST_STATE_NULL);
					gst_element_get_state(tee, NULL, NULL, GST_CLOCK_TIME_NONE);
				}
			}
		}

		if(context->activated)
		{
 			if(type == PDevice::AudioIn || type == PDevice::VideoIn)
			{
				GstElement *queue = context->element;

				// FIXME: until we fix this, we only support 1 ref
				// get tee and prepare srcpad
				/*GstPad *sinkpad = gst_element_get_pad(queue, "sink");
				GstPad *srcpad = gst_pad_get_peer(sinkpad);
				gst_object_unref(GST_OBJECT(sinkpad));
				gst_element_release_request_pad(tee, srcpad);
				gst_object_unref(GST_OBJECT(srcpad));*/

				// set queue to null state
				gst_element_set_state(queue, GST_STATE_NULL);
				gst_element_get_state(queue, NULL, NULL, GST_CLOCK_TIME_NONE);

				context->activated = false;
			}
		}

		if(activated && refs == 1)
		{
 			if(type == PDevice::AudioIn || type == PDevice::VideoIn)
				activated = false;
		}
#endif
		// FIXME
		context->activated = false;
		activated = false;
	}

	void update()
	{
		// TODO: change video properties based on options
	}
};

class PipelineContext::Private
{
public:
	GstElement *pipeline;
	bool activated;
	QSet<PipelineDevice*> devices;

	Private() :
		activated(false)
	{
		pipeline = gst_pipeline_new(NULL);
	}

	~Private()
	{
		Q_ASSERT(devices.isEmpty());
		deactivate();
		g_object_unref(G_OBJECT(pipeline));
	}

	void activate()
	{
		if(!activated)
		{
			gst_element_set_state(pipeline, GST_STATE_PLAYING);
			//gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
			activated = true;
		}
	}

	void deactivate()
	{
		if(activated)
		{
			gst_element_set_state(pipeline, GST_STATE_NULL);
			gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
			activated = false;
		}
	}
};

PipelineContext::PipelineContext()
{
	d = new Private;
}

PipelineContext::~PipelineContext()
{
	delete d;
}

void PipelineContext::activate()
{
	d->activate();
}

void PipelineContext::deactivate()
{
	d->deactivate();
}

GstElement *PipelineContext::element()
{
	return d->pipeline;
}

//----------------------------------------------------------------------------
// PipelineDeviceContext
//----------------------------------------------------------------------------
PipelineDeviceContext::PipelineDeviceContext()
{
	d = new PipelineDeviceContextPrivate;
	d->device = 0;
}

PipelineDeviceContext *PipelineDeviceContext::create(PipelineContext *pipeline, const QString &id, PDevice::Type type, const PipelineDeviceOptions &opts)
{
	PipelineDeviceContext *that = new PipelineDeviceContext;

	that->d->pipeline = pipeline;
	that->d->opts = opts;
	that->d->activated = false;

	// see if we're already using this device, so we can attempt to share
	PipelineDevice *dev = 0;
	foreach(PipelineDevice *i, pipeline->d->devices)
	{
		if(i->id == id && i->type == type)
		{
			dev = i;
			break;
		}
	}

	if(!dev)
	{
		dev = new PipelineDevice(id, type, that->d);
		if(!dev->bin)
		{
			delete dev;
			delete that;
			return 0;
		}

		pipeline->d->devices += dev;
	}
	else
	{
		// FIXME: make sharing work
		//dev->addRef(that->d);

		delete that;
		return 0;
	}

	that->d->device = dev;

#ifdef PIPELINE_DEBUG
	printf("Readying %s:[%s], refs=%d\n", type_to_str(dev->type), qPrintable(dev->id), dev->refs);
#endif
	return that;
}

PipelineDeviceContext::~PipelineDeviceContext()
{
	PipelineDevice *dev = d->device;

	if(dev)
	{
		dev->removeRef(d);
#ifdef PIPELINE_DEBUG
		printf("Releasing %s:[%s], refs=%d\n", type_to_str(dev->type), qPrintable(dev->id), dev->refs);
#endif
		if(dev->refs == 0)
		{
			d->pipeline->d->devices.remove(dev);
			delete dev;
		}
	}

	delete d;
}

void PipelineDeviceContext::activate()
{
	d->device->activate(d);
}

void PipelineDeviceContext::deactivate()
{
	d->device->deactivate(d);
}

GstElement *PipelineDeviceContext::element()
{
	return d->element;
}

void PipelineDeviceContext::setOptions(const PipelineDeviceOptions &opts)
{
	d->opts = opts;
	d->device->update();
}

}
