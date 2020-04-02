/*
 * Copyright (C) 2008 Ole Andr� Vadla Ravn�s <ole.andre.ravnas@tandberg.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef UNICODE
#undef UNICODE
#endif

#include "kshelpers.h"

#include <ksmedia.h>
#include <setupapi.h>

GST_DEBUG_CATEGORY_EXTERN(gst_ks_debug);
#define GST_CAT_DEFAULT gst_ks_debug

gboolean ks_is_valid_handle(HANDLE h) { return (h != INVALID_HANDLE_VALUE && h != NULL); }

GList *ks_enumerate_devices(const GUID *category)
{
    GList *  result = NULL;
    HDEVINFO devinfo;
    gint     i;

    devinfo = SetupDiGetClassDevsW(category, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (!ks_is_valid_handle(devinfo))
        return NULL; /* no devices */

    for (i = 0;; i++) {
        BOOL                     success;
        SP_DEVICE_INTERFACE_DATA if_data = {
            0,
        };
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *if_detail_data;
        DWORD                              if_detail_data_size;
        SP_DEVINFO_DATA                    devinfo_data = {
            0,
        };
        DWORD req_size;

        if_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        success = SetupDiEnumDeviceInterfaces(devinfo, NULL, category, i, &if_data);
        if (!success) /* all devices enumerated? */
            break;

        if_detail_data_size    = (MAX_PATH - 1) * sizeof(gunichar2);
        if_detail_data         = g_malloc0(if_detail_data_size);
        if_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);

        success = SetupDiGetDeviceInterfaceDetailW(devinfo, &if_data, if_detail_data, if_detail_data_size, &req_size,
                                                   &devinfo_data);
        if (success) {
            KsDeviceEntry *entry;
            WCHAR          buf[512];

            entry        = g_new0(KsDeviceEntry, 1);
            entry->index = i;
            entry->path  = g_utf16_to_utf8(if_detail_data->DevicePath, -1, NULL, NULL, NULL);

            if (SetupDiGetDeviceRegistryPropertyW(devinfo, &devinfo_data, SPDRP_FRIENDLYNAME, NULL, (BYTE *)buf,
                                                  sizeof(buf), NULL)) {
                entry->name = g_utf16_to_utf8(buf, -1, NULL, NULL, NULL);
            }

            if (entry->name == NULL) {
                if (SetupDiGetDeviceRegistryPropertyW(devinfo, &devinfo_data, SPDRP_DEVICEDESC, NULL, (BYTE *)buf,
                                                      sizeof(buf), NULL)) {
                    entry->name = g_utf16_to_utf8(buf, -1, NULL, NULL, NULL);
                }
            }

            if (entry->name != NULL)
                result = g_list_prepend(result, entry);
            else
                ks_device_entry_free(entry);
        }

        g_free(if_detail_data);
    }

    SetupDiDestroyDeviceInfoList(devinfo);

    return g_list_reverse(result);
}

void ks_device_entry_free(KsDeviceEntry *entry)
{
    if (entry == NULL)
        return;

    g_free(entry->path);
    g_free(entry->name);

    g_free(entry);
}

void ks_device_list_free(GList *devices)
{
    GList *cur;

    for (cur = devices; cur != NULL; cur = cur->next)
        ks_device_entry_free(cur->data);

    g_list_free(devices);
}

static gboolean ks_sync_device_io_control(HANDLE device, gulong io_control_code, gpointer in_buffer,
                                          gulong in_buffer_size, gpointer out_buffer, gulong out_buffer_size,
                                          gulong *bytes_returned)
{
    OVERLAPPED overlapped = {
        0,
    };
    BOOL success;

    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    success = DeviceIoControl(device, io_control_code, in_buffer, in_buffer_size, out_buffer, out_buffer_size,
                              bytes_returned, &overlapped);
    if (!success && GetLastError() == ERROR_IO_PENDING)
        success = GetOverlappedResult(device, &overlapped, bytes_returned, TRUE);

    CloseHandle(overlapped.hEvent);

    return success ? TRUE : FALSE;
}

gboolean ks_filter_get_pin_property(HANDLE filter_handle, gulong pin_id, GUID prop_set, gulong prop_id, gpointer value,
                                    gulong value_size)
{
    KSP_PIN prop = {
        0,
    };
    DWORD bytes_returned = 0;

    prop.PinId          = pin_id;
    prop.Property.Set   = prop_set;
    prop.Property.Id    = prop_id;
    prop.Property.Flags = KSPROPERTY_TYPE_GET;

    return ks_sync_device_io_control(filter_handle, IOCTL_KS_PROPERTY, &prop, sizeof(prop), value, value_size,
                                     &bytes_returned);
}

gboolean ks_filter_get_pin_property_multi(HANDLE filter_handle, gulong pin_id, GUID prop_set, gulong prop_id,
                                          KSMULTIPLE_ITEM **items)
{
    KSP_PIN prop = {
        0,
    };
    DWORD    items_size = 0, bytes_written = 0;
    gboolean ret;

    *items = NULL;

    prop.PinId          = pin_id;
    prop.Property.Set   = prop_set;
    prop.Property.Id    = prop_id;
    prop.Property.Flags = KSPROPERTY_TYPE_GET;

    ret = ks_sync_device_io_control(filter_handle, IOCTL_KS_PROPERTY, &prop.Property, sizeof(prop), NULL, 0,
                                    &items_size);
    if (!ret) {
        DWORD err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER && err != ERROR_MORE_DATA)
            goto error;
    }

    *items = g_malloc0(items_size);

    ret = ks_sync_device_io_control(filter_handle, IOCTL_KS_PROPERTY, &prop, sizeof(prop), *items, items_size,
                                    &bytes_written);
    if (!ret)
        goto error;

    return ret;

error:
    g_free(*items);
    *items = NULL;

    return FALSE;
}

gboolean ks_object_query_property(HANDLE handle, GUID prop_set, gulong prop_id, gulong prop_flags, gpointer *value,
                                  gulong *value_size)
{
    KSPROPERTY prop = {
        0,
    };
    DWORD    req_value_size = 0, bytes_written = 0;
    gboolean ret;

    *value = NULL;

    prop.Set   = prop_set;
    prop.Id    = prop_id;
    prop.Flags = prop_flags;

    if (value_size == NULL || *value_size == 0) {
        ret = ks_sync_device_io_control(handle, IOCTL_KS_PROPERTY, &prop, sizeof(prop), NULL, 0, &req_value_size);
        if (!ret) {
            DWORD err = GetLastError();
            if (err != ERROR_INSUFFICIENT_BUFFER && err != ERROR_MORE_DATA)
                goto error;
        }
    } else {
        req_value_size = *value_size;
    }

    *value = g_malloc0(req_value_size);

    ret = ks_sync_device_io_control(handle, IOCTL_KS_PROPERTY, &prop, sizeof(prop), *value, req_value_size,
                                    &bytes_written);
    if (!ret)
        goto error;

    if (value_size != NULL)
        *value_size = bytes_written;

    return ret;

error:
    g_free(*value);
    *value = NULL;

    if (value_size != NULL)
        *value_size = 0;

    return FALSE;
}

gboolean ks_object_get_property(HANDLE handle, GUID prop_set, gulong prop_id, gpointer *value, gulong *value_size)
{
    return ks_object_query_property(handle, prop_set, prop_id, KSPROPERTY_TYPE_GET, value, value_size);
}

gboolean ks_object_set_property(HANDLE handle, GUID prop_set, gulong prop_id, gpointer value, gulong value_size)
{
    KSPROPERTY prop = {
        0,
    };
    DWORD bytes_returned;

    prop.Set   = prop_set;
    prop.Id    = prop_id;
    prop.Flags = KSPROPERTY_TYPE_SET;

    return ks_sync_device_io_control(handle, IOCTL_KS_PROPERTY, &prop, sizeof(prop), value, value_size,
                                     &bytes_returned);
}

gboolean ks_object_get_supported_property_sets(HANDLE handle, GUID **propsets, gulong *len)
{
    gulong size = 0;

    *propsets = NULL;
    *len      = 0;

    if (ks_object_query_property(handle, GUID_NULL, 0, KSPROPERTY_TYPE_SETSUPPORT, propsets, &size)) {
        if (size % sizeof(GUID) == 0) {
            *len = size / sizeof(GUID);
            return TRUE;
        }
    }

    g_free(*propsets);
    *propsets = NULL;
    *len      = 0;
    return FALSE;
}

gboolean ks_object_set_connection_state(HANDLE handle, KSSTATE state)
{
    return ks_object_set_property(handle, KSPROPSETID_Connection, KSPROPERTY_CONNECTION_STATE, &state, sizeof(state));
}

const gchar *ks_state_to_string(KSSTATE state)
{
    switch (state) {
    case KSSTATE_STOP:
        return "KSSTATE_STOP";
    case KSSTATE_ACQUIRE:
        return "KSSTATE_ACQUIRE";
    case KSSTATE_PAUSE:
        return "KSSTATE_PAUSE";
    case KSSTATE_RUN:
        return "KSSTATE_RUN";
    default:
        g_assert_not_reached();
    }

    return "UNKNOWN";
}

#define CHECK_OPTIONS_FLAG(flag)                                                                                       \
    if (flags & KSSTREAM_HEADER_OPTIONSF_##flag) {                                                                     \
        if (str->len > 0)                                                                                              \
            g_string_append(str, "|");                                                                                 \
        g_string_append(str, G_STRINGIFY(flag));                                                                       \
        flags &= ~KSSTREAM_HEADER_OPTIONSF_##flag;                                                                     \
    }

gchar *ks_options_flags_to_string(gulong flags)
{
    gchar *  ret;
    GString *str;

    str = g_string_sized_new(128);

    CHECK_OPTIONS_FLAG(SPLICEPOINT);
    CHECK_OPTIONS_FLAG(PREROLL);
    CHECK_OPTIONS_FLAG(DATADISCONTINUITY);
    CHECK_OPTIONS_FLAG(TYPECHANGED);
    CHECK_OPTIONS_FLAG(TIMEVALID);
    CHECK_OPTIONS_FLAG(TIMEDISCONTINUITY);
    CHECK_OPTIONS_FLAG(FLUSHONPAUSE);
    CHECK_OPTIONS_FLAG(DURATIONVALID);
    CHECK_OPTIONS_FLAG(ENDOFSTREAM);
    CHECK_OPTIONS_FLAG(BUFFEREDTRANSFER);
    CHECK_OPTIONS_FLAG(VRAM_DATA_TRANSFER);
    CHECK_OPTIONS_FLAG(LOOPEDDATA);

    if (flags != 0)
        g_string_append_printf(str, "|0x%08x", flags);

    ret = str->str;
    g_string_free(str, FALSE);

    return ret;
}

typedef struct {
    const GUID   guid;
    const gchar *name;
} KsPropertySetMapping;

#ifndef STATIC_KSPROPSETID_GM
#define STATIC_KSPROPSETID_GM 0xAF627536, 0xE719, 0x11D2, 0x8A, 0x1D, 0x00, 0x60, 0x97, 0xD2, 0xDF, 0x5D
#endif
#ifndef STATIC_KSPROPSETID_Jack
#define STATIC_KSPROPSETID_Jack 0x4509F757, 0x2D46, 0x4637, 0x8E, 0x62, 0xCE, 0x7D, 0xB9, 0x44, 0xF5, 0x7B
#endif

#ifndef STATIC_PROPSETID_VIDCAP_SELECTOR
#define STATIC_PROPSETID_VIDCAP_SELECTOR 0x1ABDAECA, 0x68B6, 0x4F83, 0x93, 0x71, 0xB4, 0x13, 0x90, 0x7C, 0x7B, 0x9F
#endif
#ifndef STATIC_PROPSETID_EXT_DEVICE
#define STATIC_PROPSETID_EXT_DEVICE 0xB5730A90, 0x1A2C, 0x11cf, 0x8c, 0x23, 0x00, 0xAA, 0x00, 0x6B, 0x68, 0x14
#endif
#ifndef STATIC_PROPSETID_EXT_TRANSPORT
#define STATIC_PROPSETID_EXT_TRANSPORT 0xA03CD5F0, 0x3045, 0x11cf, 0x8c, 0x44, 0x00, 0xAA, 0x00, 0x6B, 0x68, 0x14
#endif
#ifndef STATIC_PROPSETID_TIMECODE_READER
#define STATIC_PROPSETID_TIMECODE_READER 0x9B496CE1, 0x811B, 0x11cf, 0x8C, 0x77, 0x00, 0xAA, 0x00, 0x6B, 0x68, 0x14
#endif

static const KsPropertySetMapping known_property_sets[] = {
    { { STATIC_KSPROPSETID_General }, "General" },
    { { STATIC_KSPROPSETID_MediaSeeking }, "MediaSeeking" },
    { { STATIC_KSPROPSETID_Topology }, "Topology" },
    { { STATIC_KSPROPSETID_GM }, "GM" },
    { { STATIC_KSPROPSETID_Pin }, "Pin" },
    { { STATIC_KSPROPSETID_Quality }, "Quality" },
    { { STATIC_KSPROPSETID_Connection }, "Connection" },
    { { STATIC_KSPROPSETID_MemoryTransport }, "MemoryTransport" },
    { { STATIC_KSPROPSETID_StreamAllocator }, "StreamAllocator" },
    { { STATIC_KSPROPSETID_StreamInterface }, "StreamInterface" },
    { { STATIC_KSPROPSETID_Stream }, "Stream" },
    { { STATIC_KSPROPSETID_Clock }, "Clock" },

    { { STATIC_KSPROPSETID_DirectSound3DListener }, "DirectSound3DListener" },
    { { STATIC_KSPROPSETID_DirectSound3DBuffer }, "DirectSound3DBuffer" },
    { { STATIC_KSPROPSETID_Hrtf3d }, "Hrtf3d" },
    { { STATIC_KSPROPSETID_Itd3d }, "Itd3d" },
    { { STATIC_KSPROPSETID_Bibliographic }, "Bibliographic" },
    { { STATIC_KSPROPSETID_TopologyNode }, "TopologyNode" },
    { { STATIC_KSPROPSETID_RtAudio }, "RtAudio" },
    { { STATIC_KSPROPSETID_DrmAudioStream }, "DrmAudioStream" },
    { { STATIC_KSPROPSETID_Audio }, "Audio" },
    //{{STATIC_KSPROPSETID_Acoustic_Echo_Cancel}, "Acoustic_Echo_Cancel"},
    //{{STATIC_KSPROPSETID_Wave_Queued}, "Wave_Queued"},
    { { STATIC_KSPROPSETID_Wave }, "Wave" },
    //{{STATIC_KSPROPSETID_WaveTable}, "WaveTable"},
    { { STATIC_KSPROPSETID_Cyclic }, "Cyclic" },
    //{{STATIC_KSPROPSETID_Sysaudio}, "Sysaudio"},
    //{{STATIC_KSPROPSETID_Sysaudio_Pin}, "Sysaudio_Pin"},
    //{{STATIC_KSPROPSETID_AudioGfx}, "AudioGfx"},
    //{{STATIC_KSPROPSETID_Linear}, "Linear"},
    { { STATIC_KSPROPSETID_Mpeg2Vid }, "Mpeg2Vid" },
    { { STATIC_KSPROPSETID_AC3 }, "AC3" },
    { { STATIC_KSPROPSETID_AudioDecoderOut }, "AudioDecoderOut" },
    { { STATIC_KSPROPSETID_DvdSubPic }, "DvdSubPic" },
    { { STATIC_KSPROPSETID_CopyProt }, "CopyProt" },
    { { STATIC_KSPROPSETID_VBICAP_PROPERTIES }, "VBICAP_PROPERTIES" },
    { { STATIC_KSPROPSETID_VBICodecFiltering }, "VBICodecFiltering" },
    { { STATIC_KSPROPSETID_VramCapture }, "VramCapture" },
    { { STATIC_KSPROPSETID_OverlayUpdate }, "OverlayUpdate" },
    { { STATIC_KSPROPSETID_VPConfig }, "VPConfig" },
    { { STATIC_KSPROPSETID_VPVBIConfig }, "VPVBIConfig" },
    { { STATIC_KSPROPSETID_TSRateChange }, "TSRateChange" },
    { { STATIC_KSPROPSETID_Jack }, "Jack" },

    { { STATIC_PROPSETID_ALLOCATOR_CONTROL }, "ALLOCATOR_CONTROL" },
    { { STATIC_PROPSETID_VIDCAP_VIDEOPROCAMP }, "VIDCAP_VIDEOPROCAMP" },
    { { STATIC_PROPSETID_VIDCAP_SELECTOR }, "VIDCAP_SELECTOR" },
    { { STATIC_PROPSETID_TUNER }, "TUNER" },
    { { STATIC_PROPSETID_VIDCAP_VIDEOENCODER }, "VIDCAP_VIDEOENCODER" },
    { { STATIC_PROPSETID_VIDCAP_VIDEODECODER }, "VIDCAP_VIDEODECODER" },
    { { STATIC_PROPSETID_VIDCAP_CAMERACONTROL }, "VIDCAP_CAMERACONTROL" },
    { { STATIC_PROPSETID_EXT_DEVICE }, "EXT_DEVICE" },
    { { STATIC_PROPSETID_EXT_TRANSPORT }, "EXT_TRANSPORT" },
    { { STATIC_PROPSETID_TIMECODE_READER }, "TIMECODE_READER" },
    { { STATIC_PROPSETID_VIDCAP_CROSSBAR }, "VIDCAP_CROSSBAR" },
    { { STATIC_PROPSETID_VIDCAP_TVAUDIO }, "VIDCAP_TVAUDIO" },
    { { STATIC_PROPSETID_VIDCAP_VIDEOCOMPRESSION }, "VIDCAP_VIDEOCOMPRESSION" },
    { { STATIC_PROPSETID_VIDCAP_VIDEOCONTROL }, "VIDCAP_VIDEOCONTROL" },
    { { STATIC_PROPSETID_VIDCAP_DROPPEDFRAMES }, "VIDCAP_DROPPEDFRAMES" },
};

gchar *ks_property_set_to_string(const GUID *guid)
{
    guint i;

    for (i = 0; i < sizeof(known_property_sets) / sizeof(known_property_sets[0]); i++) {
        if (IsEqualGUID(guid, &known_property_sets[i].guid))
            return g_strdup_printf("KSPROPSETID_%s", known_property_sets[i].name);
    }

    return g_strdup_printf("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", guid->Data1, guid->Data2, guid->Data3,
                           guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4],
                           guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}
