/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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

/**
 * SECTION:element-ksvideosrc
 *
 * Provides low-latency video capture from WDM cameras on Windows.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v ksvideosrc do-stats=TRUE ! ffmpegcolorspace ! dshowvideosink
 * ]| Capture from a camera and render using dshowvideosink.
 * |[
 * gst-launch -v ksvideosrc do-stats=TRUE ! image/jpeg, width=640, height=480
 * ! jpegdec ! ffmpegcolorspace ! dshowvideosink
 * ]| Capture from an MJPEG camera and render using dshowvideosink.
 * </refsect2>
 */

#ifdef UNICODE
#undef UNICODE
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstksvideosrc.h"

#include "gstksclock.h"
#include "gstksvideodevice.h"
#include "kshelpers.h"
#include "ksvideohelpers.h"

#define DEFAULT_DEVICE_PATH NULL
#define DEFAULT_DEVICE_NAME NULL
#define DEFAULT_DEVICE_INDEX -1
#define DEFAULT_DO_STATS FALSE
#define DEFAULT_ENABLE_QUIRKS TRUE

enum {
    PROP_0,
    PROP_DEVICE_PATH,
    PROP_DEVICE_NAME,
    PROP_DEVICE_INDEX,
    PROP_DO_STATS,
    PROP_FPS,
    PROP_ENABLE_QUIRKS,
};

GST_DEBUG_CATEGORY(gst_ks_debug);
#define GST_CAT_DEFAULT gst_ks_debug

#define KS_WORKER_LOCK(priv) g_mutex_lock(priv->worker_lock)
#define KS_WORKER_UNLOCK(priv) g_mutex_unlock(priv->worker_lock)
#define KS_WORKER_WAIT(priv) g_cond_wait(priv->worker_notify_cond, priv->worker_lock)
#define KS_WORKER_NOTIFY(priv) g_cond_signal(priv->worker_notify_cond)
#define KS_WORKER_WAIT_FOR_RESULT(priv) g_cond_wait(priv->worker_result_cond, priv->worker_lock)
#define KS_WORKER_NOTIFY_RESULT(priv) g_cond_signal(priv->worker_result_cond)

typedef enum {
    KS_WORKER_STATE_STARTING,
    KS_WORKER_STATE_READY,
    KS_WORKER_STATE_STOPPING,
    KS_WORKER_STATE_ERROR,
} KsWorkerState;

typedef struct {
    /* Properties */
    gchar *  device_path;
    gchar *  device_name;
    gint     device_index;
    gboolean do_stats;
    gboolean enable_quirks;

    /* State */
    GstKsClock *      ksclock;
    GstKsVideoDevice *device;

    guint64      offset;
    GstClockTime prev_ts;
    gboolean     running;

    /* Worker thread */
    GThread *     worker_thread;
    GMutex *      worker_lock;
    GCond *       worker_notify_cond;
    GCond *       worker_result_cond;
    KsWorkerState worker_state;

    GstCaps *worker_pending_caps;
    gboolean worker_setcaps_result;

    gboolean worker_pending_run;
    gboolean worker_run_result;

    /* Statistics */
    GstClockTime last_sampling;
    guint        count;
    guint        fps;
} GstKsVideoSrcPrivate;

#define GST_KS_VIDEO_SRC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_KS_VIDEO_SRC, GstKsVideoSrcPrivate))

static void gst_ks_video_src_finalize(GObject *object);
static void gst_ks_video_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_ks_video_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);

static void gst_ks_video_src_reset(GstKsVideoSrc *self);

static GstStateChangeReturn gst_ks_video_src_change_state(GstElement *element, GstStateChange transition);
static gboolean             gst_ks_video_src_set_clock(GstElement *element, GstClock *clock);

static GstCaps *gst_ks_video_src_get_caps(GstBaseSrc *basesrc);
static gboolean gst_ks_video_src_set_caps(GstBaseSrc *basesrc, GstCaps *caps);
static void     gst_ks_video_src_fixate(GstBaseSrc *basesrc, GstCaps *caps);
static gboolean gst_ks_video_src_query(GstBaseSrc *basesrc, GstQuery *query);
static gboolean gst_ks_video_src_unlock(GstBaseSrc *basesrc);
static gboolean gst_ks_video_src_unlock_stop(GstBaseSrc *basesrc);

static GstFlowReturn gst_ks_video_src_create(GstPushSrc *pushsrc, GstBuffer **buffer);

GST_BOILERPLATE(GstKsVideoSrc, gst_ks_video_src, GstPushSrc, GST_TYPE_PUSH_SRC);

static void gst_ks_video_src_base_init(gpointer gclass)
{
    GstElementClass *        element_class = GST_ELEMENT_CLASS(gclass);
    static GstElementDetails element_details
        = { "KsVideoSrc", "Source/Video", "Stream data from a video capture device through Windows kernel streaming",
            "Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>\n"
            "Haakon Sporsheim <hakon.sporsheim@tandberg.com>" };

    gst_element_class_set_details(element_class, &element_details);

    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, ks_video_get_all_caps()));
}

static void gst_ks_video_src_class_init(GstKsVideoSrcClass *klass)
{
    GObjectClass *   gobject_class    = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstKsVideoSrcPrivate));

    gobject_class->finalize     = gst_ks_video_src_finalize;
    gobject_class->get_property = gst_ks_video_src_get_property;
    gobject_class->set_property = gst_ks_video_src_set_property;

    gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_ks_video_src_change_state);
    gstelement_class->set_clock    = GST_DEBUG_FUNCPTR(gst_ks_video_src_set_clock);

    gstbasesrc_class->get_caps    = GST_DEBUG_FUNCPTR(gst_ks_video_src_get_caps);
    gstbasesrc_class->set_caps    = GST_DEBUG_FUNCPTR(gst_ks_video_src_set_caps);
    gstbasesrc_class->fixate      = GST_DEBUG_FUNCPTR(gst_ks_video_src_fixate);
    gstbasesrc_class->query       = GST_DEBUG_FUNCPTR(gst_ks_video_src_query);
    gstbasesrc_class->unlock      = GST_DEBUG_FUNCPTR(gst_ks_video_src_unlock);
    gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_ks_video_src_unlock_stop);

    gstpushsrc_class->create = GST_DEBUG_FUNCPTR(gst_ks_video_src_create);

    g_object_class_install_property(gobject_class, PROP_DEVICE_PATH,
                                    g_param_spec_string("device-path", "Device Path", "The device path",
                                                        DEFAULT_DEVICE_PATH,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_DEVICE_NAME,
                                    g_param_spec_string("device-name", "Device Name", "The human-readable device name",
                                                        DEFAULT_DEVICE_NAME,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_DEVICE_INDEX,
                                    g_param_spec_int("device-index", "Device Index", "The zero-based device index", -1,
                                                     G_MAXINT, DEFAULT_DEVICE_INDEX,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_DO_STATS,
                                    g_param_spec_boolean("do-stats", "Enable statistics",
                                                         "Enable logging of statistics", DEFAULT_DO_STATS,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_FPS,
                                    g_param_spec_int("fps", "Frames per second",
                                                     "Last measured framerate, if statistics are enabled", -1, G_MAXINT,
                                                     -1, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_ENABLE_QUIRKS,
                                    g_param_spec_boolean("enable-quirks", "Enable quirks",
                                                         "Enable driver-specific quirks", DEFAULT_ENABLE_QUIRKS,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    GST_DEBUG_CATEGORY_INIT(gst_ks_debug, "ksvideosrc", 0, "Kernel streaming video source");
}

static void gst_ks_video_src_init(GstKsVideoSrc *self, GstKsVideoSrcClass *gclass)
{
    GstKsVideoSrcPrivate *priv    = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    GstBaseSrc *          basesrc = GST_BASE_SRC(self);

    gst_base_src_set_live(basesrc, TRUE);
    gst_base_src_set_format(basesrc, GST_FORMAT_TIME);

    gst_ks_video_src_reset(self);

    priv->device_path   = DEFAULT_DEVICE_PATH;
    priv->device_name   = DEFAULT_DEVICE_NAME;
    priv->device_index  = DEFAULT_DEVICE_INDEX;
    priv->do_stats      = DEFAULT_DO_STATS;
    priv->enable_quirks = DEFAULT_ENABLE_QUIRKS;
}

static void gst_ks_video_src_finalize(GObject *object)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(object);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    g_free(priv->device_name);
    g_free(priv->device_path);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_ks_video_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(object);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_value_set_string(value, priv->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_value_set_string(value, priv->device_name);
        break;
    case PROP_DEVICE_INDEX:
        g_value_set_int(value, priv->device_index);
        break;
    case PROP_DO_STATS:
        GST_OBJECT_LOCK(object);
        g_value_set_boolean(value, priv->do_stats);
        GST_OBJECT_UNLOCK(object);
        break;
    case PROP_FPS:
        GST_OBJECT_LOCK(object);
        g_value_set_int(value, priv->fps);
        GST_OBJECT_UNLOCK(object);
        break;
    case PROP_ENABLE_QUIRKS:
        g_value_set_boolean(value, priv->enable_quirks);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_ks_video_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(object);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_free(priv->device_path);
        priv->device_path = g_value_dup_string(value);
        break;
    case PROP_DEVICE_NAME:
        g_free(priv->device_name);
        priv->device_name = g_value_dup_string(value);
        break;
    case PROP_DEVICE_INDEX:
        priv->device_index = g_value_get_int(value);
        break;
    case PROP_DO_STATS:
        GST_OBJECT_LOCK(object);
        priv->do_stats = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(object);
        break;
    case PROP_ENABLE_QUIRKS:
        priv->enable_quirks = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_ks_video_src_reset(GstKsVideoSrc *self)
{
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    /* Reset statistics */
    priv->last_sampling = GST_CLOCK_TIME_NONE;
    priv->count         = 0;
    priv->fps           = -1;

    /* Reset timestamping state */
    priv->offset  = 0;
    priv->prev_ts = GST_CLOCK_TIME_NONE;

    priv->running = FALSE;
}

static void gst_ks_video_src_apply_driver_quirks(GstKsVideoSrc *self)
{
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    HMODULE               mod;

    /*
     * Logitech's driver software injects the following DLL into all processes
     * spawned. This DLL does some nasty tricks, sitting in between the
     * application and the low-level ntdll API (NtCreateFile, NtClose,
     * NtDeviceIoControlFile, NtDuplicateObject, etc.), making all sorts
     * of assumptions.
     *
     * The only regression that this quirk causes is that the video effects
     * feature doesn't work.
     */
    mod = GetModuleHandle("LVPrcInj.dll");
    if (mod != NULL) {
        GST_DEBUG_OBJECT(self, "Logitech DLL detected, neutralizing it");

        /*
         * We know that no-one's actually keeping this handle around to decrement
         * its reference count, so we'll take care of that job. The DLL's DllMain
         * implementation takes care of rolling back changes when it gets unloaded,
         * so this seems to be the cleanest and most future-proof way that we can
         * get rid of it...
         */
        FreeLibrary(mod);

        /* Paranoia: verify that it's no longer there */
        mod = GetModuleHandle("LVPrcInj.dll");
        if (mod != NULL)
            GST_WARNING_OBJECT(self, "failed to neutralize Logitech DLL");
    }
}

static gboolean gst_ks_video_src_open_device(GstKsVideoSrc *self)
{
    GstKsVideoSrcPrivate *priv   = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    GstKsVideoDevice *    device = NULL;
    GList *               devices, *cur;

    g_assert(priv->device == NULL);

    devices = ks_enumerate_devices(&KSCATEGORY_VIDEO);
    if (devices == NULL)
        goto error_no_devices;

    for (cur = devices; cur != NULL; cur = cur->next) {
        KsDeviceEntry *entry = cur->data;

        GST_DEBUG_OBJECT(self, "device %d: name='%s' path='%s'", entry->index, entry->name, entry->path);
    }

    for (cur = devices; cur != NULL && device == NULL; cur = cur->next) {
        KsDeviceEntry *entry = cur->data;
        gboolean       match;

        if (priv->device_path != NULL) {
            match = g_strcasecmp(entry->path, priv->device_path) == 0;
        } else if (priv->device_name != NULL) {
            match = g_strcasecmp(entry->name, priv->device_name) == 0;
        } else if (priv->device_index >= 0) {
            match = entry->index == priv->device_index;
        } else {
            match = TRUE; /* pick the first entry */
        }

        if (match) {
            priv->ksclock = g_object_new(GST_TYPE_KS_CLOCK, NULL);
            if (priv->ksclock != NULL && gst_ks_clock_open(priv->ksclock)) {
                GstClock *clock = GST_ELEMENT_CLOCK(self);
                if (clock != NULL)
                    gst_ks_clock_provide_master_clock(priv->ksclock, clock);
            } else {
                GST_WARNING_OBJECT(self, "failed to create/open KsClock");
                g_object_unref(priv->ksclock);
                priv->ksclock = NULL;
            }

            device = g_object_new(GST_TYPE_KS_VIDEO_DEVICE, "clock", priv->ksclock, "device-path", entry->path, NULL);
        }

        ks_device_entry_free(entry);
    }

    g_list_free(devices);

    if (device == NULL)
        goto error_no_match;

    if (!gst_ks_video_device_open(device))
        goto error_open;

    priv->device = device;

    return TRUE;

    /* ERRORS */
error_no_devices : {
    GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("No video capture devices found"), (NULL));
    return FALSE;
}
error_no_match : {
    if (priv->device_path != NULL) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS,
                          ("Specified video capture device with path '%s' not found", priv->device_path), (NULL));
    } else if (priv->device_name != NULL) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS,
                          ("Specified video capture device with name '%s' not found", priv->device_name), (NULL));
    } else {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS,
                          ("Specified video capture device with index %d not found", priv->device_index), (NULL));
    }
    return FALSE;
}
error_open : {
    GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, ("Failed to open device"), (NULL));
    g_object_unref(device);
    return FALSE;
}
}

static void gst_ks_video_src_close_device(GstKsVideoSrc *self)
{
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    g_assert(priv->device != NULL);

    gst_ks_video_device_close(priv->device);
    g_object_unref(priv->device);
    priv->device = NULL;

    if (priv->ksclock != NULL) {
        gst_ks_clock_close(priv->ksclock);
        g_object_unref(priv->ksclock);
        priv->ksclock = NULL;
    }

    gst_ks_video_src_reset(self);
}

/*
 * Worker thread that takes care of starting, configuring and stopping things.
 *
 * This is needed because Logitech's driver software injects a DLL that
 * intercepts API functions like NtCreateFile, NtClose, NtDeviceIoControlFile
 * and NtDuplicateObject so that they can provide in-place video effects to
 * existing applications. Their assumption is that at least one thread tainted
 * by their code stays around for the lifetime of the capture.
 */
static gpointer gst_ks_video_src_worker_func(gpointer data)
{
    GstKsVideoSrc *       self = data;
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    if (!gst_ks_video_src_open_device(self))
        goto open_failed;

    KS_WORKER_LOCK(priv);
    priv->worker_state = KS_WORKER_STATE_READY;
    KS_WORKER_NOTIFY_RESULT(priv);

    while (priv->worker_state != KS_WORKER_STATE_STOPPING) {
        KS_WORKER_WAIT(priv);

        if (priv->worker_pending_caps != NULL) {
            priv->worker_setcaps_result = gst_ks_video_device_set_caps(priv->device, priv->worker_pending_caps);

            priv->worker_pending_caps = NULL;
            KS_WORKER_NOTIFY_RESULT(priv);
        } else if (priv->worker_pending_run) {
            if (priv->ksclock != NULL)
                gst_ks_clock_start(priv->ksclock);
            priv->worker_run_result = gst_ks_video_device_set_state(priv->device, KSSTATE_RUN);

            priv->worker_pending_run = FALSE;
            KS_WORKER_NOTIFY_RESULT(priv);
        }
    }

    KS_WORKER_UNLOCK(priv);

    gst_ks_video_src_close_device(self);

    return NULL;

    /* ERRORS */
open_failed : {
    KS_WORKER_LOCK(priv);
    priv->worker_state = KS_WORKER_STATE_ERROR;
    KS_WORKER_NOTIFY_RESULT(priv);
    KS_WORKER_UNLOCK(priv);

    return NULL;
}
}

static gboolean gst_ks_video_src_start_worker(GstKsVideoSrc *self)
{
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    gboolean              result;

    priv->worker_lock        = g_mutex_new();
    priv->worker_notify_cond = g_cond_new();
    priv->worker_result_cond = g_cond_new();

    priv->worker_pending_caps = NULL;
    priv->worker_pending_run  = FALSE;

    priv->worker_state  = KS_WORKER_STATE_STARTING;
    priv->worker_thread = g_thread_create(gst_ks_video_src_worker_func, self, TRUE, NULL);

    KS_WORKER_LOCK(priv);
    while (priv->worker_state < KS_WORKER_STATE_READY)
        KS_WORKER_WAIT_FOR_RESULT(priv);
    result = priv->worker_state == KS_WORKER_STATE_READY;
    KS_WORKER_UNLOCK(priv);

    return result;
}

static void gst_ks_video_src_stop_worker(GstKsVideoSrc *self)
{
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    KS_WORKER_LOCK(priv);
    priv->worker_state = KS_WORKER_STATE_STOPPING;
    KS_WORKER_NOTIFY(priv);
    KS_WORKER_UNLOCK(priv);

    g_thread_join(priv->worker_thread);
    priv->worker_thread = NULL;

    g_cond_free(priv->worker_result_cond);
    priv->worker_result_cond = NULL;
    g_cond_free(priv->worker_notify_cond);
    priv->worker_notify_cond = NULL;
    g_mutex_free(priv->worker_lock);
    priv->worker_lock = NULL;
}

static GstStateChangeReturn gst_ks_video_src_change_state(GstElement *element, GstStateChange transition)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(element);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    GstStateChangeReturn  ret;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        if (priv->enable_quirks)
            gst_ks_video_src_apply_driver_quirks(self);
        if (!gst_ks_video_src_start_worker(self))
            goto open_failed;
        break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
        gst_ks_video_src_stop_worker(self);
        break;
    }

    return ret;

    /* ERRORS */
open_failed : {
    gst_ks_video_src_stop_worker(self);
    return GST_STATE_CHANGE_FAILURE;
}
}

static gboolean gst_ks_video_src_set_clock(GstElement *element, GstClock *clock)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(element);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    GST_OBJECT_LOCK(element);
    if (clock != NULL && priv->ksclock != NULL)
        gst_ks_clock_provide_master_clock(priv->ksclock, clock);
    GST_OBJECT_UNLOCK(element);

    return TRUE;
}

static GstCaps *gst_ks_video_src_get_caps(GstBaseSrc *basesrc)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(basesrc);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    if (priv->device != NULL)
        return gst_ks_video_device_get_available_caps(priv->device);
    else
        return NULL; /* BaseSrc will return template caps */
}

static gboolean gst_ks_video_src_set_caps(GstBaseSrc *basesrc, GstCaps *caps)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(basesrc);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    if (priv->device == NULL)
        return FALSE;

    KS_WORKER_LOCK(priv);
    priv->worker_pending_caps = caps;
    KS_WORKER_NOTIFY(priv);
    while (priv->worker_pending_caps == caps)
        KS_WORKER_WAIT_FOR_RESULT(priv);
    KS_WORKER_UNLOCK(priv);

    return priv->worker_setcaps_result;
}

static void gst_ks_video_src_fixate(GstBaseSrc *basesrc, GstCaps *caps)
{
    GstStructure *structure = gst_caps_get_structure(caps, 0);

    gst_structure_fixate_field_nearest_int(structure, "width", G_MAXINT);
    gst_structure_fixate_field_nearest_int(structure, "height", G_MAXINT);
    gst_structure_fixate_field_nearest_fraction(structure, "framerate", G_MAXINT, 1);
}

static gboolean gst_ks_video_src_query(GstBaseSrc *basesrc, GstQuery *query)
{
    GstKsVideoSrc *       self   = GST_KS_VIDEO_SRC(basesrc);
    GstKsVideoSrcPrivate *priv   = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    gboolean              result = FALSE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        GstClockTime min_latency, max_latency;

        if (priv->device == NULL)
            goto beach;

        result = gst_ks_video_device_get_latency(priv->device, &min_latency, &max_latency);
        if (!result)
            goto beach;

        GST_DEBUG_OBJECT(self, "reporting latency of min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
                         GST_TIME_ARGS(min_latency), GST_TIME_ARGS(max_latency));

        gst_query_set_latency(query, TRUE, min_latency, max_latency);
        break;
    }
    default:
        result = GST_BASE_SRC_CLASS(parent_class)->query(basesrc, query);
        break;
    }

beach:
    return result;
}

static gboolean gst_ks_video_src_unlock(GstBaseSrc *basesrc)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(basesrc);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    GST_DEBUG_OBJECT(self, "%s", G_STRFUNC);

    gst_ks_video_device_cancel(priv->device);
    return TRUE;
}

static gboolean gst_ks_video_src_unlock_stop(GstBaseSrc *basesrc)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(basesrc);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);

    GST_DEBUG_OBJECT(self, "%s", G_STRFUNC);

    gst_ks_video_device_cancel_stop(priv->device);
    return TRUE;
}

static gboolean gst_ks_video_src_timestamp_buffer(GstKsVideoSrc *self, GstBuffer *buf, GstClockTime presentation_time)
{
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    GstClockTime          duration;
    GstClock *            clock;
    GstClockTime          timestamp;

    duration = gst_ks_video_device_get_duration(priv->device);

    GST_OBJECT_LOCK(self);
    clock = GST_ELEMENT_CLOCK(self);
    if (clock != NULL) {
        gst_object_ref(clock);
        timestamp = GST_ELEMENT(self)->base_time;

        if (GST_CLOCK_TIME_IS_VALID(presentation_time)) {
            if (presentation_time > GST_ELEMENT(self)->base_time)
                presentation_time -= GST_ELEMENT(self)->base_time;
            else
                presentation_time = 0;
        }
    } else {
        timestamp = GST_CLOCK_TIME_NONE;
    }
    GST_OBJECT_UNLOCK(self);

    if (clock != NULL) {

        /* The time according to the current clock */
        timestamp = gst_clock_get_time(clock) - timestamp;
        if (timestamp > duration)
            timestamp -= duration;
        else
            timestamp = 0;

        if (GST_CLOCK_TIME_IS_VALID(presentation_time)) {
            /*
             * We don't use this for anything yet, need to ponder how to deal
             * with pins that use an internal clock and timestamp from 0.
             */
            GstClockTimeDiff diff = GST_CLOCK_DIFF(presentation_time, timestamp);
            GST_DEBUG_OBJECT(self, "diff between gst and driver timestamp: %" G_GINT64_FORMAT, diff);
        }

        gst_object_unref(clock);
        clock = NULL;

        /* Unless it's the first frame, align the current timestamp on a multiple
         * of duration since the previous */
        if (GST_CLOCK_TIME_IS_VALID(priv->prev_ts)) {
            GstClockTime delta;
            guint        delta_remainder, delta_offset;

            /* REVISIT: I've seen this happen with the GstSystemClock on Windows,
             *          scary... */
            if (timestamp < priv->prev_ts) {
                GST_WARNING_OBJECT(self, "clock is ticking backwards");
                return FALSE;
            }

            /* Round to a duration boundary */
            delta           = timestamp - priv->prev_ts;
            delta_remainder = delta % duration;

            if (delta_remainder < duration / 3)
                timestamp -= delta_remainder;
            else
                timestamp += duration - delta_remainder;

            /* How many frames are we off then? */
            delta        = timestamp - priv->prev_ts;
            delta_offset = delta / duration;

            if (delta_offset == 1) /* perfect */
                GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_DISCONT);
            else if (delta_offset > 1) {
                guint lost = delta_offset - 1;
                GST_INFO_OBJECT(self, "lost %d frame%s, setting discont flag", lost, (lost > 1) ? "s" : "");
                GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);
            } else if (delta_offset == 0) { /* overproduction, skip this frame */
                GST_INFO_OBJECT(self, "skipping frame");
                return FALSE;
            }

            priv->offset += delta_offset;
        }

        priv->prev_ts = timestamp;
    }

    GST_BUFFER_OFFSET(buf)     = priv->offset;
    GST_BUFFER_OFFSET_END(buf) = GST_BUFFER_OFFSET(buf) + 1;
    GST_BUFFER_TIMESTAMP(buf)  = timestamp;
    GST_BUFFER_DURATION(buf)   = duration;

    return TRUE;
}

static void gst_ks_video_src_update_statistics(GstKsVideoSrc *self)
{
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    GstClock *            clock;

    GST_OBJECT_LOCK(self);
    clock = GST_ELEMENT_CLOCK(self);
    if (clock != NULL)
        gst_object_ref(clock);
    GST_OBJECT_UNLOCK(self);

    if (clock != NULL) {
        GstClockTime now = gst_clock_get_time(clock);
        gst_object_unref(clock);

        priv->count++;

        if (GST_CLOCK_TIME_IS_VALID(priv->last_sampling)) {
            if (now - priv->last_sampling >= GST_SECOND) {
                GST_OBJECT_LOCK(self);
                priv->fps = priv->count;
                GST_OBJECT_UNLOCK(self);

                g_object_notify(G_OBJECT(self), "fps");

                priv->last_sampling = now;
                priv->count         = 0;
            }
        } else {
            priv->last_sampling = now;
        }
    }
}

static GstFlowReturn gst_ks_video_src_create(GstPushSrc *pushsrc, GstBuffer **buffer)
{
    GstKsVideoSrc *       self = GST_KS_VIDEO_SRC(pushsrc);
    GstKsVideoSrcPrivate *priv = GST_KS_VIDEO_SRC_GET_PRIVATE(self);
    guint                 buf_size;
    GstCaps *             caps;
    GstBuffer *           buf = NULL;
    GstFlowReturn         result;
    GstClockTime          presentation_time;
    gulong                error_code;
    gchar *               error_str;

    g_assert(priv->device != NULL);

    if (!gst_ks_video_device_has_caps(priv->device))
        goto error_no_caps;

    buf_size = gst_ks_video_device_get_frame_size(priv->device);
    g_assert(buf_size);

    caps = gst_pad_get_negotiated_caps(GST_BASE_SRC_PAD(self));
    if (caps == NULL)
        goto error_no_caps;
    result = gst_pad_alloc_buffer(GST_BASE_SRC_PAD(self), priv->offset, buf_size, caps, &buf);
    gst_caps_unref(caps);
    if (G_UNLIKELY(result != GST_FLOW_OK))
        goto error_alloc_buffer;

    if (G_UNLIKELY(!priv->running)) {
        KS_WORKER_LOCK(priv);
        priv->worker_pending_run = TRUE;
        KS_WORKER_NOTIFY(priv);
        while (priv->worker_pending_run)
            KS_WORKER_WAIT_FOR_RESULT(priv);
        priv->running = priv->worker_run_result;
        KS_WORKER_UNLOCK(priv);

        if (!priv->running)
            goto error_start_capture;
    }

    do {
        gulong bytes_read;

        result = gst_ks_video_device_read_frame(priv->device, GST_BUFFER_DATA(buf), buf_size, &bytes_read,
                                                &presentation_time, &error_code, &error_str);
        if (G_UNLIKELY(result != GST_FLOW_OK))
            goto error_read_frame;

        GST_BUFFER_SIZE(buf) = bytes_read;
    } while (!gst_ks_video_src_timestamp_buffer(self, buf, presentation_time));

    if (G_UNLIKELY(priv->do_stats))
        gst_ks_video_src_update_statistics(self);

    gst_ks_video_device_postprocess_frame(priv->device, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));

    *buffer = buf;
    return GST_FLOW_OK;

    /* ERRORS */
error_no_caps : {
    GST_ELEMENT_ERROR(self, CORE, NEGOTIATION, ("not negotiated"), ("maybe setcaps failed?"));

    return GST_FLOW_ERROR;
}
error_start_capture : {
    GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, ("could not start capture"),
                      ("failed to change pin state to KSSTATE_RUN"));

    return GST_FLOW_ERROR;
}
error_alloc_buffer : {
    GST_ELEMENT_ERROR(self, CORE, PAD, ("alloc_buffer failed"), (NULL));

    return result;
}
error_read_frame : {
    if (result != GST_FLOW_WRONG_STATE && result != GST_FLOW_UNEXPECTED) {
        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("read failed: %s [0x%08x]", error_str, error_code),
                          ("gst_ks_video_device_read_frame failed"));
    }

    g_free(error_str);
    gst_buffer_unref(buf);

    return result;
}
}

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin, "ksvideosrc", GST_RANK_NONE, GST_TYPE_KS_VIDEO_SRC);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, "winks", "Windows kernel streaming plugin", plugin_init,
                  VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
