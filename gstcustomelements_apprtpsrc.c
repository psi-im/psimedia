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
#include <string.h>

GST_BOILERPLATE(GstAppRtpSrc, gst_apprtpsrc, GstPushSrc, GST_TYPE_PUSH_SRC);

static void gst_apprtpsrc_finalize(GObject *obj);
static gboolean gst_apprtpsrc_stop(GstBaseSrc *src);
static GstFlowReturn gst_apprtpsrc_create(GstBaseSrc *src, guint64 offset, guint size, GstBuffer **buf);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY
	);

void gst_apprtpsrc_base_init(gpointer gclass)
{
	static GstElementDetails element_details = GST_ELEMENT_DETAILS(
		"Application RTP Source",
		"Generic/PluginTemplate",
		"Generic Template Element",
		"AUTHOR_NAME AUTHOR_EMAIL"
	);
	GstElementClass *element_class = GST_ELEMENT_CLASS(gclass);

	gst_element_class_add_pad_template(element_class,
		gst_static_pad_template_get(&src_factory));
	gst_element_class_set_details(element_class, &element_details);
}

// class init
void gst_apprtpsrc_class_init(GstAppRtpSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstBaseSrcClass *basesrc_class;

	gobject_class = (GObjectClass *)klass;
	gobject_class->finalize = gst_apprtpsrc_finalize;

	basesrc_class = (GstBaseSrcClass *)klass;
	basesrc_class->stop = gst_apprtpsrc_stop;
	basesrc_class->create = gst_apprtpsrc_create;
}

// instance init
void gst_apprtpsrc_init(GstAppRtpSrc *src, GstAppRtpSrcClass *gclass)
{
	(void)gclass;

	src->cur_buf = 0;
	src->push_mutex = g_mutex_new();
	src->push_cond = g_cond_new();
	src->quit = FALSE;
}

// destruct
void gst_apprtpsrc_finalize(GObject *obj)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)obj;

	if(src->cur_buf)
		gst_buffer_unref(src->cur_buf);
	g_mutex_free(src->push_mutex);
	g_cond_free(src->push_cond);
}

gboolean gst_apprtpsrc_stop(GstBaseSrc *bsrc)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)bsrc;

	g_mutex_lock(src->push_mutex);
	src->quit = TRUE;
	g_cond_signal(src->push_cond);
	g_mutex_unlock(src->push_mutex);

	return TRUE;
}

GstFlowReturn gst_apprtpsrc_create(GstBaseSrc *bsrc, guint64 offset, guint size, GstBuffer **buf)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)bsrc;

	(void)offset;
	(void)size;

	// the assumption here is that every buffer is a complete rtp
	//   packet, ready for processing

	// i believe the app is supposed to block on this call waiting for
	//   data

	g_mutex_lock(src->push_mutex);

	while(!src->cur_buf && !src->quit)
		g_cond_wait(src->push_cond, src->push_mutex);

	if(src->quit)
	{
		g_mutex_unlock(src->push_mutex);
		return GST_FLOW_ERROR;
	}

	*buf = src->cur_buf;
	src->cur_buf = 0;

	g_mutex_unlock(src->push_mutex);

	return GST_FLOW_OK;
}

void gst_apprtpsrc_packet_push(GstAppRtpSrc *src, const unsigned char *buf, int size)
{
	g_mutex_lock(src->push_mutex);

	if(src->cur_buf)
	{
		gst_buffer_unref(src->cur_buf);
		src->cur_buf = 0;
	}

	// ignore zero-byte packets
	if(size < 1)
	{
		g_mutex_unlock(src->push_mutex);
		return;
	}

	src->cur_buf = gst_buffer_new_and_alloc(size);
	memcpy(GST_BUFFER_DATA(src->cur_buf), buf, size);

	g_cond_signal(src->push_cond);
	g_mutex_unlock(src->push_mutex);
}
