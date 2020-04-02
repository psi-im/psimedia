/*
 * Farsight Voice+Video library
 *
 *  Copyright 2008 Collabora Ltd
 *  Copyright 2008 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#include "../speexdsp/speexdsp.h"
#include "../speexdsp/speexechoprobe.h"
#include <gst/audio/audio.h>
#include <string.h>

/* dsp/probe use these to discover each other */
GStaticMutex       global_mutex = G_STATIC_MUTEX_INIT;
GstSpeexDSP *      global_dsp   = NULL;
GstSpeexEchoProbe *global_probe = NULL;

static gboolean plugin_init(GstPlugin *plugin)
{
    if (!gst_element_register(plugin, "speexdsp", GST_RANK_NONE, GST_TYPE_SPEEX_DSP)) {
        return FALSE;
    }
    if (!gst_element_register(plugin, "speexechoprobe", GST_RANK_NONE, GST_TYPE_SPEEX_ECHO_PROBE)) {
        return FALSE;
    }

    return TRUE;
}

void gstelements_speexdsp_register()
{
    gst_plugin_register_static(GST_VERSION_MAJOR, GST_VERSION_MINOR, "speexdsp", "Voice preprocessing using libspeex",
                               plugin_init, "1.0.4", "LGPL", "my-application", "my-application",
                               "http://www.my-application.net/");
}
