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

#include "devices.h"

#include <QSet>
#include <QSize>
#include <QStringList>
#include <gst/gst.h>

namespace PsiMedia {

#if 0
// for elements that we can't enumerate devices for, we need a way to ensure
//   that at least the default device works
// FIXME: why do we have both this function and test_video() ?
static bool test_element(const QString &element_name)
{
	GstElement *e = gst_element_factory_make(element_name.toLatin1().data(), NULL);
	if(!e)
		return 0;

	gst_element_set_state(e, GST_STATE_READY);
	int ret = gst_element_get_state(e, NULL, NULL, GST_CLOCK_TIME_NONE);

	gst_element_set_state(e, GST_STATE_NULL);
	gst_element_get_state(e, NULL, NULL, GST_CLOCK_TIME_NONE);
	g_object_unref(G_OBJECT(e));

	if(ret != GST_STATE_CHANGE_SUCCESS)
		return false;

	return true;
}
#endif

// copied from gst-inspect-1.0. perfect for identifying devices
static gchar *
get_launch_line (::GstDevice * device)
{
  static const char *const ignored_propnames[] =
      { "name", "parent", "direction", "template", "caps", NULL };
  GString *launch_line;
  GstElement *element;
  GstElement *pureelement;
  GParamSpec **properties, *property;
  GValue value = G_VALUE_INIT;
  GValue pvalue = G_VALUE_INIT;
  guint i, number_of_properties;
  GstElementFactory *factory;

  element = gst_device_create_element (device, NULL);

  if (!element)
    return NULL;

  factory = gst_element_get_factory (element);
  if (!factory) {
    gst_object_unref (element);
    return NULL;
  }

  if (!gst_plugin_feature_get_name (factory)) {
    gst_object_unref (element);
    return NULL;
  }

  launch_line = g_string_new (gst_plugin_feature_get_name (factory));

  pureelement = gst_element_factory_create (factory, NULL);

  /* get paramspecs and show non-default properties */
  properties =
      g_object_class_list_properties (G_OBJECT_GET_CLASS (element),
      &number_of_properties);
  if (properties) {
    for (i = 0; i < number_of_properties; i++) {
      gint j;
      gboolean ignore = FALSE;
      property = properties[i];

      /* skip some properties */
      if ((property->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE)
        continue;

      for (j = 0; ignored_propnames[j]; j++)
        if (!g_strcmp0 (ignored_propnames[j], property->name))
          ignore = TRUE;

      if (ignore)
        continue;

      /* Can't use _param_value_defaults () because sub-classes modify the
       * values already.
       */

      g_value_init (&value, property->value_type);
      g_value_init (&pvalue, property->value_type);
      g_object_get_property (G_OBJECT (element), property->name, &value);
      g_object_get_property (G_OBJECT (pureelement), property->name, &pvalue);
      if (gst_value_compare (&value, &pvalue) != GST_VALUE_EQUAL) {
        gchar *valuestr = gst_value_serialize (&value);

        if (!valuestr) {
          GST_WARNING ("Could not serialize property %s:%s",
              GST_OBJECT_NAME (element), property->name);
          g_free (valuestr);
          goto next;
        }

        g_string_append_printf (launch_line, " %s=%s",
            property->name, valuestr);
        g_free (valuestr);

      }

    next:
      g_value_unset (&value);
      g_value_unset (&pvalue);
    }
    g_free (properties);
  }

  gst_object_unref (element);
  gst_object_unref (pureelement);

  return g_string_free (launch_line, FALSE);
}

class DeviceMonitor
{
    GstDeviceMonitor *_monitor = nullptr;
    QList<GstDevice> _devices;
    PlatformDeviceMonitor *_platform = nullptr;

    static gboolean onChangeGstCB(GstBus * bus, GstMessage * message, gpointer user_data)
    {
        auto monObj = reinterpret_cast<DeviceMonitor*>(user_data);
        monObj->onChangeDMCallback(bus, message);
        return G_SOURCE_CONTINUE;
    }

    void onChangeDMCallback(GstBus * bus, GstMessage * message)
    {
        Q_UNUSED(bus)
        ::GstDevice *device;
        gchar *name;

        switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_DEVICE_ADDED:
            gst_message_parse_device_added (message, &device);
            name = gst_device_get_display_name (device);
            g_print("Device added: %s\n", name);
            g_free (name);
            gst_object_unref (device);
            break;
        case GST_MESSAGE_DEVICE_REMOVED:
            gst_message_parse_device_removed (message, &device);
            name = gst_device_get_display_name (device);
            g_print("Device removed: %s\n", name);
            g_free (name);
            gst_object_unref (device);
            break;
        default:
            break;
        }
    }

    void updateDevList()
    {
        QSet<QString> ids;
        _devices.clear();
        GList *devs = gst_device_monitor_get_devices(_monitor);
        GList *dev = devs;
        bool videoSrcFirst = true;
        bool audioSrcFirst = true;
        bool audioSinkFirst = true;
        for (; dev != NULL; dev = dev->next) {
            PsiMedia::GstDevice d;

            ::GstDevice *gdev = (::GstDevice*)(dev->data);
            gchar *ll = get_launch_line(gdev);
            if (ll) {
                auto e = gst_parse_launch(ll, NULL);
                if (e) {
                    d.id = QString::fromUtf8(ll);
                    gst_object_unref(e);
                }
                g_free(ll);
                if (d.id.isEmpty() || d.id.endsWith(QLatin1String(".monitor")))
                    continue;
            }

            gchar *name = gst_device_get_display_name(gdev);
            d.name = QString::fromUtf8(name);
            g_free(name);

            if (gst_device_has_classes(gdev, "Audio/Source")) {
                d.type = PDevice::AudioIn;
                d.isDefault = audioSrcFirst;
                audioSrcFirst = false;
            }

            if (gst_device_has_classes(gdev, "Audio/Sink")) {
                d.type = PDevice::AudioOut;
                d.isDefault = audioSinkFirst;
                audioSinkFirst = false;
            }

            if (gst_device_has_classes(gdev, "Video/Source")) {
                d.type = PDevice::VideoIn;
                d.isDefault = videoSrcFirst;
                videoSrcFirst = false;
            }

            _devices.append(d);
            ids.insert(d.id);
        }
        g_list_free(devs);

        if (_platform) {
            auto l = _platform->getDevices();
            for (auto const &d: l) {
                if (!ids.contains(d.id)) {
                    _devices.append(d);
                }
            }
        }

        for (auto const &d: _devices) {
            qDebug("found dev: %s (%s)", qPrintable(d.name), qPrintable(d.id));
        }
    }

public:
    DeviceMonitor()
    {
#if defined(Q_OS_LINUX)
        _platform = new PlatformDeviceMonitor;
#endif
        _monitor = gst_device_monitor_new();

        GstBus *bus = gst_device_monitor_get_bus (_monitor);
        gst_bus_add_watch (bus, onChangeGstCB, this);
        gst_object_unref (bus);

        gst_device_monitor_add_filter (_monitor, "Audio/Sink", NULL);
        gst_device_monitor_add_filter (_monitor, "Audio/Source", NULL);

        GstCaps *caps;
        caps = gst_caps_new_empty_simple ("video/x-raw");
        gst_device_monitor_add_filter (_monitor, "Video/Source", caps);
        caps = gst_caps_new_empty_simple ("image/jpeg");
        gst_device_monitor_add_filter (_monitor, "Video/Source", caps);
        gst_caps_unref (caps);

        updateDevList();
    }

    QList<GstDevice> devices(PDevice::Type type)
    {
        QList<GstDevice> ret;
        for (auto const &d: _devices) {
            if (d.type == type) ret.append(d);
        }
        return ret;
    }
};

QList<GstDevice> devices_list(PDevice::Type type)
{
    static auto dm = new DeviceMonitor();
    return dm->devices(type);
}

GstElement *devices_makeElement(const QString &id, PDevice::Type type, QSize *captureSize)
{
    return gst_parse_launch(id.toLatin1().data(), NULL);
    // TODO check if it correponds to passed type.
    // TODO drop captureSize
}

}
