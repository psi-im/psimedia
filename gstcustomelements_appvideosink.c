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

GST_BOILERPLATE(GstAppVideoSink, gst_appvideosink, GstVideoSink, GST_TYPE_VIDEO_SINK);

static GstFlowReturn gst_appvideosink_render(GstBaseSink *sink, GstBuffer *buffer);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw-rgb, "
		"framerate = (fraction) [ 0/1, 2147483647/1 ], "
		"width = (int) [ 1, 2147483647 ], "
		"height = (int) [ 1, 2147483647 ]"
		)
	);

void gst_appvideosink_base_init(gpointer gclass)
{
	static GstElementDetails element_details = GST_ELEMENT_DETAILS(
		"Application Video Sink",
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
void gst_appvideosink_class_init(GstAppVideoSinkClass *klass)
{
	GstBaseSinkClass *basesink_class;
	basesink_class = (GstBaseSinkClass *)klass;
	basesink_class->render = gst_appvideosink_render;
}

// instance init
void gst_appvideosink_init(GstAppVideoSink *sink, GstAppVideoSinkClass *gclass)
{
	(void)gclass;

	sink->appdata = 0;
	sink->show_frame = 0;
}

GstFlowReturn gst_appvideosink_render(GstBaseSink *sink, GstBuffer *buffer)
{
	GstAppVideoSink *self = (GstAppVideoSink *)sink;

	GstCaps *caps = GST_BUFFER_CAPS(buffer);
	GstStructure *structure = gst_caps_get_structure(caps, 0);

	// get width and height
	int width, height;
	if(!gst_structure_get_int(structure, "width", &width) ||
		!gst_structure_get_int(structure, "height", &height))
	{
		return GST_FLOW_ERROR;
	}

	// make sure buffer size matches width * height * 24 bit rgb
	int size = GST_BUFFER_SIZE(buffer);
	if(width * height * 3 != size)
		return GST_FLOW_ERROR;

	if(self->show_frame)
		self->show_frame(width, height, GST_BUFFER_DATA(buffer), self->appdata);

	return GST_FLOW_OK;
}
