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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../videomaxrate/videomaxrate.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "videomaxrate", GST_RANK_NONE,
          GST_TYPE_VIDEOMAXRATE))
    return FALSE;

  return TRUE;
}

void gstelements_videomaxrate_register()
{
  gst_plugin_register_static(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videomaxrate",
    "Drop extra frames",
    plugin_init,
    "1.0.0",
    "LGPL",
    "my-application",
    "my-application",
    "http://www.my-application.net/"
    );
}
