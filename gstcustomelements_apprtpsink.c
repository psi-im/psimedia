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

#include "gstcustomelements.h"

#include "gstboilerplatefixed.h"

GST_BOILERPLATE(GstAppRtpSink, gst_apprtpsink, GstBaseSink, GST_TYPE_BASE_SINK);

static GstFlowReturn gst_apprtpsink_render(GstBaseSink *sink, GstBuffer *buffer);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY
	);

void gst_apprtpsink_base_init(gpointer gclass)
{
	static GstElementDetails element_details = GST_ELEMENT_DETAILS(
		"Application RTP Sink",
		"Generic/PluginTemplate",
		"Generic Template Element",
		"AUTHOR_NAME AUTHOR_EMAIL"
	);
	GstElementClass *element_class = GST_ELEMENT_CLASS(gclass);

	gst_element_class_add_pad_template(element_class,
		gst_static_pad_template_get(&sink_factory));
	gst_element_class_set_details(element_class, &element_details);
}

// class init
void gst_apprtpsink_class_init(GstAppRtpSinkClass *klass)
{
	GstBaseSinkClass *basesink_class;
	basesink_class = (GstBaseSinkClass *)klass;
	basesink_class->render = gst_apprtpsink_render;
}

// instance init
void gst_apprtpsink_init(GstAppRtpSink *sink, GstAppRtpSinkClass *gclass)
{
	(void)gclass;

	sink->appdata = 0;
	sink->packet_ready = 0;
}

GstFlowReturn gst_apprtpsink_render(GstBaseSink *sink, GstBuffer *buffer)
{
	GstAppRtpSink *self = (GstAppRtpSink *)sink;

	// the assumption here is that every buffer is a complete rtp
	//   packet, ready for sending

	if(self->packet_ready)
		self->packet_ready(GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer), self->appdata);

	return GST_FLOW_OK;
}
