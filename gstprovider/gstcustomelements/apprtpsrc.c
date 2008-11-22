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

#define APPRTPSRC_MAX_BUF_COUNT 32

GST_BOILERPLATE(GstAppRtpSrc, gst_apprtpsrc, GstPushSrc, GST_TYPE_PUSH_SRC);

enum
{
	PROP_0,

	PROP_CAPS,

	PROP_LAST
};

static void gst_apprtpsrc_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_apprtpsrc_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_apprtpsrc_finalize(GObject *obj);
static gboolean gst_apprtpsrc_unlock(GstBaseSrc *src);
static gboolean gst_apprtpsrc_unlock_stop(GstBaseSrc *src);
static GstCaps *gst_apprtpsrc_get_caps(GstBaseSrc *src);
static GstFlowReturn gst_apprtpsrc_create(GstPushSrc *src, GstBuffer **buf);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY
	);

void gst_apprtpsrc_base_init(gpointer gclass)
{
	static GstElementDetails element_details = GST_ELEMENT_DETAILS(
		"Application RTP Source",
		"Generic/Source",
		"Receive RTP packets from the application",
		"Justin Karneges <justin@affinix.com>"
	);
	GstElementClass *element_class = GST_ELEMENT_CLASS(gclass);

	gst_element_class_add_pad_template(element_class,
		gst_static_pad_template_get(&src_template));
	gst_element_class_set_details(element_class, &element_details);
}

// class init
void gst_apprtpsrc_class_init(GstAppRtpSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstBaseSrcClass *basesrc_class;
	GstPushSrcClass *pushsrc_class;

	gobject_class = (GObjectClass *)klass;
	basesrc_class = (GstBaseSrcClass *)klass;
	pushsrc_class = (GstPushSrcClass *)klass;

	gobject_class->set_property = gst_apprtpsrc_set_property;
	gobject_class->get_property = gst_apprtpsrc_get_property;
	gobject_class->finalize = gst_apprtpsrc_finalize;

	g_object_class_install_property(gobject_class, PROP_CAPS,
		g_param_spec_boxed("caps", "Caps",
		"The caps of the source pad", GST_TYPE_CAPS,
		G_PARAM_READWRITE));

	basesrc_class->unlock = gst_apprtpsrc_unlock;
	basesrc_class->unlock_stop = gst_apprtpsrc_unlock_stop;
	basesrc_class->get_caps = gst_apprtpsrc_get_caps;

	pushsrc_class->create = gst_apprtpsrc_create;
}

// instance init
void gst_apprtpsrc_init(GstAppRtpSrc *src, GstAppRtpSrcClass *gclass)
{
	(void)gclass;

	src->buffers = g_queue_new();
	src->push_mutex = g_mutex_new();
	src->push_cond = g_cond_new();
	src->quit = FALSE; // not flushing
	src->caps = 0;

	// set up the base (adapted from udpsrc)

	// configure basesrc to be a live source
	gst_base_src_set_live(GST_BASE_SRC(src), TRUE);
	// make basesrc output a segment in time
	gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);
	// make basesrc set timestamps on outgoing buffers based on the
	//   running_time when they were captured
	gst_base_src_set_do_timestamp(GST_BASE_SRC(src), TRUE);
}

// destruct
static void my_foreach_func(gpointer data, gpointer user_data)
{
	GstBuffer *buf = (GstBuffer *)data;
	(void)user_data;
	gst_buffer_unref(buf);
}

void gst_apprtpsrc_finalize(GObject *obj)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)obj;

	g_queue_foreach(src->buffers, my_foreach_func, NULL);
	g_queue_free(src->buffers);
	g_mutex_free(src->push_mutex);
	g_cond_free(src->push_cond);
	if(src->caps)
		gst_caps_unref(src->caps);

	G_OBJECT_CLASS(parent_class)->finalize(obj);
}

gboolean gst_apprtpsrc_unlock(GstBaseSrc *bsrc)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)bsrc;

	g_mutex_lock(src->push_mutex);
	src->quit = TRUE; // flushing
	g_cond_signal(src->push_cond);
	g_mutex_unlock(src->push_mutex);

	return TRUE;
}

gboolean gst_apprtpsrc_unlock_stop(GstBaseSrc *bsrc)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)bsrc;

	g_mutex_lock(src->push_mutex);
	src->quit = FALSE; // not flushing
	g_mutex_unlock(src->push_mutex);

	return TRUE;
}

GstCaps *gst_apprtpsrc_get_caps(GstBaseSrc *bsrc)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)bsrc;

	if(src->caps)
		return gst_caps_ref(src->caps);
	else
		return gst_caps_new_any();
}

GstFlowReturn gst_apprtpsrc_create(GstPushSrc *bsrc, GstBuffer **buf)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)bsrc;

	// the assumption here is that every buffer is a complete rtp
	//   packet, ready for processing

	// i believe the app is supposed to block on this call waiting for
	//   data

	g_mutex_lock(src->push_mutex);

	while(g_queue_is_empty(src->buffers) && !src->quit)
		g_cond_wait(src->push_cond, src->push_mutex);

	// flushing?
	if(src->quit)
	{
		g_mutex_unlock(src->push_mutex);
		return GST_FLOW_WRONG_STATE;
	}

	*buf = (GstBuffer *)g_queue_pop_head(src->buffers);
	gst_buffer_set_caps(*buf, src->caps);

	g_mutex_unlock(src->push_mutex);

	return GST_FLOW_OK;
}

void gst_apprtpsrc_packet_push(GstAppRtpSrc *src, const unsigned char *buf, int size)
{
	GstBuffer *newbuf;

	g_mutex_lock(src->push_mutex);

	// if buffer is full, eat the oldest to make room
	if(g_queue_get_length(src->buffers) >= APPRTPSRC_MAX_BUF_COUNT)
		g_queue_pop_head(src->buffers);

	// ignore zero-byte packets
	if(size < 1)
	{
		g_mutex_unlock(src->push_mutex);
		return;
	}

	newbuf = gst_buffer_new_and_alloc(size);
	memcpy(GST_BUFFER_DATA(newbuf), buf, size);
	g_queue_push_tail(src->buffers, newbuf);

	g_cond_signal(src->push_cond);
	g_mutex_unlock(src->push_mutex);
}

void gst_apprtpsrc_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)obj;
	(void)pspec;

	switch(prop_id)
	{
		case PROP_CAPS:
		{
			const GstCaps *new_caps_val = gst_value_get_caps(value);
			GstCaps *new_caps;
			GstCaps *old_caps;
			if(new_caps_val == NULL)
				new_caps = gst_caps_new_any();
			else
				new_caps = gst_caps_copy(new_caps_val);
			old_caps = src->caps;
			src->caps = new_caps;
			if(old_caps)
				gst_caps_unref(old_caps);
			gst_pad_set_caps(GST_BASE_SRC(src)->srcpad, new_caps);
			break;
		}
		default:
			break;
	}
}

void gst_apprtpsrc_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstAppRtpSrc *src = (GstAppRtpSrc *)obj;

	switch(prop_id)
	{
		case PROP_CAPS:
			gst_value_set_caps(value, src->caps);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			break;
	}
}
