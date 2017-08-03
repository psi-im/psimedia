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

static gboolean register_elements(GstPlugin *plugin)
{
	if(!gst_element_register(plugin, "appvideosink",
		GST_RANK_NONE, GST_TYPE_APPVIDEOSINK))
	{
		return FALSE;
	}

	if(!gst_element_register(plugin, "apprtpsrc",
		GST_RANK_NONE, GST_TYPE_APPRTPSRC))
	{
		return FALSE;
	}

	if(!gst_element_register(plugin, "apprtpsink",
		GST_RANK_NONE, GST_TYPE_APPRTPSINK))
	{
		return FALSE;
	}

	return TRUE;
}

void gstcustomelements_register()
{
	gst_plugin_register_static(
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"my-private-plugins",
		"Private elements of my application",
		register_elements,
		"1.0.4",
		"LGPL",
		"my-application",
		"my-application",
		"http://www.my-application.net/"
		);
}
