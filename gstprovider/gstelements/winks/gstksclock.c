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

#ifdef UNICODE
#undef UNICODE
#endif

#include "gstksclock.h"

#include "kshelpers.h"

GST_DEBUG_CATEGORY_EXTERN(gst_ks_debug);
#define GST_CAT_DEFAULT gst_ks_debug

typedef struct {
    GMutex *mutex;
    GCond * client_cond;
    GCond * worker_cond;

    HANDLE clock_handle;

    gboolean open;
    gboolean closing;
    KSSTATE  state;

    GThread *worker_thread;
    gboolean worker_running;
    gboolean worker_initialized;

    GstClock *master_clock;
} GstKsClockPrivate;

#define GST_KS_CLOCK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_KS_CLOCK, GstKsClockPrivate))

#define GST_KS_CLOCK_LOCK() g_mutex_lock(priv->mutex)
#define GST_KS_CLOCK_UNLOCK() g_mutex_unlock(priv->mutex)

static void gst_ks_clock_dispose(GObject *object);
static void gst_ks_clock_finalize(GObject *object);

GST_BOILERPLATE(GstKsClock, gst_ks_clock, GObject, G_TYPE_OBJECT);

static void gst_ks_clock_base_init(gpointer gclass) {}

static void gst_ks_clock_class_init(GstKsClockClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstKsClockPrivate));

    gobject_class->dispose  = gst_ks_clock_dispose;
    gobject_class->finalize = gst_ks_clock_finalize;
}

static void gst_ks_clock_init(GstKsClock *self, GstKsClockClass *gclass)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    priv->mutex       = g_mutex_new();
    priv->client_cond = g_cond_new();
    priv->worker_cond = g_cond_new();

    priv->clock_handle = INVALID_HANDLE_VALUE;

    priv->open    = FALSE;
    priv->closing = FALSE;
    priv->state   = KSSTATE_STOP;

    priv->worker_thread      = NULL;
    priv->worker_running     = FALSE;
    priv->worker_initialized = FALSE;

    priv->master_clock = NULL;
}

static void gst_ks_clock_dispose(GObject *object)
{
    GstKsClock *       self = GST_KS_CLOCK(object);
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    g_assert(!priv->open);

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void gst_ks_clock_finalize(GObject *object)
{
    GstKsClock *       self = GST_KS_CLOCK(object);
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    g_cond_free(priv->worker_cond);
    g_cond_free(priv->client_cond);
    g_mutex_free(priv->mutex);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_ks_clock_close_unlocked(GstKsClock *self);

gboolean gst_ks_clock_open(GstKsClock *self)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);
    gboolean           ret  = FALSE;
    GList *            devices;
    KsDeviceEntry *    device;
    KSSTATE            state;

    GST_KS_CLOCK_LOCK();

    g_assert(!priv->open);

    priv->state = KSSTATE_STOP;

    devices = ks_enumerate_devices(&KSCATEGORY_CLOCK);
    if (devices == NULL)
        goto error;

    device = devices->data;

    priv->clock_handle = CreateFile(device->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (!ks_is_valid_handle(priv->clock_handle))
        goto error;

    state = KSSTATE_STOP;
    if (!ks_object_set_property(priv->clock_handle, KSPROPSETID_Clock, KSPROPERTY_CLOCK_STATE, &state, sizeof(state)))
        goto error;

    ks_device_list_free(devices);
    priv->open = TRUE;

    GST_KS_CLOCK_UNLOCK();
    return TRUE;

error:
    ks_device_list_free(devices);
    gst_ks_clock_close_unlocked(self);

    GST_KS_CLOCK_UNLOCK();
    return FALSE;
}

static gboolean gst_ks_clock_set_state_unlocked(GstKsClock *self, KSSTATE state)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);
    KSSTATE            initial_state;
    gint               addend;

    g_assert(priv->open);

    if (state == priv->state)
        return TRUE;

    initial_state = priv->state;
    addend        = (state > priv->state) ? 1 : -1;

    GST_DEBUG("Initiating clock state change from %s to %s", ks_state_to_string(priv->state),
              ks_state_to_string(state));

    while (priv->state != state) {
        KSSTATE next_state = priv->state + addend;

        GST_DEBUG("Changing clock state from %s to %s", ks_state_to_string(priv->state),
                  ks_state_to_string(next_state));

        if (ks_object_set_property(priv->clock_handle, KSPROPSETID_Clock, KSPROPERTY_CLOCK_STATE, &next_state,
                                   sizeof(next_state))) {
            priv->state = next_state;

            GST_DEBUG("Changed clock state to %s", ks_state_to_string(priv->state));
        } else {
            GST_WARNING("Failed to change clock state to %s", ks_state_to_string(next_state));
            return FALSE;
        }
    }

    GST_DEBUG("Finished clock state change from %s to %s", ks_state_to_string(initial_state),
              ks_state_to_string(state));

    return TRUE;
}

static void gst_ks_clock_close_unlocked(GstKsClock *self)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    if (priv->closing)
        return;

    priv->closing = TRUE;

    if (priv->worker_thread != NULL) {
        priv->worker_running = FALSE;
        g_cond_signal(priv->worker_cond);

        GST_KS_CLOCK_UNLOCK();
        g_thread_join(priv->worker_thread);
        priv->worker_thread = NULL;
        GST_KS_CLOCK_LOCK();
    }

    gst_ks_clock_set_state_unlocked(self, KSSTATE_STOP);

    if (ks_is_valid_handle(priv->clock_handle)) {
        CloseHandle(priv->clock_handle);
        priv->clock_handle = INVALID_HANDLE_VALUE;
    }

    if (priv->master_clock != NULL) {
        gst_object_unref(priv->master_clock);
        priv->master_clock = NULL;
    }

    priv->open    = FALSE;
    priv->closing = FALSE;
}

void gst_ks_clock_close(GstKsClock *self)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    GST_KS_CLOCK_LOCK();
    gst_ks_clock_close_unlocked(self);
    GST_KS_CLOCK_UNLOCK();
}

HANDLE
gst_ks_clock_get_handle(GstKsClock *self)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);
    HANDLE             handle;

    GST_KS_CLOCK_LOCK();
    g_assert(priv->open);
    handle = priv->clock_handle;
    GST_KS_CLOCK_UNLOCK();

    return handle;
}

void gst_ks_clock_prepare(GstKsClock *self)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    GST_KS_CLOCK_LOCK();
    if (priv->state < KSSTATE_PAUSE)
        gst_ks_clock_set_state_unlocked(self, KSSTATE_PAUSE);
    GST_KS_CLOCK_UNLOCK();
}

static gpointer gst_ks_clock_worker_thread_func(gpointer data)
{
    GstKsClock *       self = GST_KS_CLOCK(data);
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    GST_KS_CLOCK_LOCK();

    gst_ks_clock_set_state_unlocked(self, KSSTATE_RUN);

    while (priv->worker_running) {
        if (priv->master_clock != NULL) {
            GstClockTime now = gst_clock_get_time(priv->master_clock);
            now /= 100;

            if (ks_object_set_property(priv->clock_handle, KSPROPSETID_Clock, KSPROPERTY_CLOCK_TIME, &now,
                                       sizeof(now))) {
                GST_DEBUG("clock synchronized");
                gst_object_unref(priv->master_clock);
                priv->master_clock = NULL;
            } else {
                GST_WARNING("failed to synchronize clock");
            }
        }

        if (!priv->worker_initialized) {
            priv->worker_initialized = TRUE;
            g_cond_signal(priv->client_cond);
        }

        g_cond_wait(priv->worker_cond, priv->mutex);
    }

    priv->worker_initialized = FALSE;
    GST_KS_CLOCK_UNLOCK();

    return NULL;
}

void gst_ks_clock_start(GstKsClock *self)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    GST_KS_CLOCK_LOCK();

    if (priv->worker_thread == NULL) {
        priv->worker_running     = TRUE;
        priv->worker_initialized = FALSE;

        priv->worker_thread = g_thread_create(gst_ks_clock_worker_thread_func, self, TRUE, NULL);
    }

    while (!priv->worker_initialized)
        g_cond_wait(priv->client_cond, priv->mutex);

    GST_KS_CLOCK_UNLOCK();
}

void gst_ks_clock_provide_master_clock(GstKsClock *self, GstClock *master_clock)
{
    GstKsClockPrivate *priv = GST_KS_CLOCK_GET_PRIVATE(self);

    GST_KS_CLOCK_LOCK();

    gst_object_ref(master_clock);
    if (priv->master_clock != NULL)
        gst_object_unref(priv->master_clock);
    priv->master_clock = master_clock;
    g_cond_signal(priv->worker_cond);

    GST_KS_CLOCK_UNLOCK();
}
