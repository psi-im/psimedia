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

#include <QSize>
#include <QStringList>
#include <gst/gst.h>
#if 0
#include <gst/interfaces/propertyprobe.h>
#endif
#include "deviceenum/deviceenum.h"

namespace PsiMedia {
#if 0
class GstDeviceProbeValue
{
public:
	QString id;
	QString name;
};


static QList<GstDeviceProbeValue> device_probe(GstElement *e)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS(e);
	if(!g_object_class_find_property(klass, "device") || !GST_IS_PROPERTY_PROBE(e))
		return QList<GstDeviceProbeValue>();

	GstPropertyProbe *probe = GST_PROPERTY_PROBE(e);
	if(!probe)
		return QList<GstDeviceProbeValue>();

	const GParamSpec *pspec = gst_property_probe_get_property(probe, "device");
	if(!pspec)
		return QList<GstDeviceProbeValue>();

	QList<GstDeviceProbeValue> out;

	GValueArray *list = gst_property_probe_probe_and_get_values(probe, pspec);
	if(list)
	{
		for(int n = 0; n < (int)list->n_values; ++n)
		{
			GValue *i = g_value_array_get_nth(list, n);

			// FIXME: "device" isn't always a string
			gchar *name;
			g_object_set(G_OBJECT(e), "device", g_value_get_string(i), NULL);
			g_object_get(G_OBJECT(e), "device-name", &name, NULL);

			GstDeviceProbeValue dev;
			dev.id = QString::fromUtf8(g_value_get_string(i));
			dev.name = QString::fromUtf8(name);
			g_free(name);

			out += dev;
		}

		g_value_array_free(list);
	}

	return out;
}

static bool element_should_use_probe(const QString &element_name)
{
	// we can enumerate devices in two ways.  one is via gst property
	//   probing and the other is through our own DeviceEnum code.
	//   since gst property probing is "the future", we'll take a
	//   probe-by-default approach, and only use DeviceEnum for specific
	//   elements

	// these should use DeviceEnum
	if(element_name == "alsasrc" ||
		element_name == "alsasink" ||
		element_name == "osssrc" ||
		element_name == "osssink" ||
		element_name == "v4lsrc" ||
		element_name == "v4l2src" ||
		element_name == "osxaudiosrc" ||
		element_name == "osxaudiosink" ||
		element_name == "ksvideosrc")
	{
		return false;
	}
	// all else probe
	else
		return true;
}


static QList<DeviceEnum::Item> device_enum(const QString &driver, PDevice::Type type)
{
	if(type == PDevice::AudioOut)
		return DeviceEnum::audioOutputItems(driver);
	else if(type == PDevice::AudioIn)
		return DeviceEnum::audioInputItems(driver);
	else // PDevice::VideoIn
		return DeviceEnum::videoInputItems(driver);
}

static QString id_part_escape(const QString &in)
{
	QString out;
	for(int n = 0; n < in.length(); ++n)
	{
		if(in[n] == '\\')
			out += "\\\\";
		else if(in[n] == ',')
			out += "\\c";
		else
			out += in[n];
	}
	return out;
}
#endif
static QString id_part_unescape(const QString &in)
{
	QString out;
	for(int n = 0; n < in.length(); ++n)
	{
		if(in[n] == '\\')
		{
			if(n + 1 >= in.length())
				return QString();

			++n;
			if(in[n] == '\\')
				out += '\\';
			else if(in[n] == 'c')
				out += ',';
			else
				return QString();
		}
		else
			out += in[n];
	}
	return out;
}
#if 0
static QString resolution_to_string(const QSize &size)
{
	return QString::number(size.width()) + 'x' + QString::number(size.height());
}
#endif
static QSize string_to_resolution(const QString &in)
{
	int at = in.indexOf('x');
	if(at == -1)
		return QSize();

	QString ws = in.mid(0, at);
	QString hs = in.mid(at + 1);

	bool ok;
	int w = ws.toInt(&ok);
	if(!ok)
		return QSize();

	int h = hs.toInt(&ok);
	if(!ok)
		return QSize();

	return QSize(w, h);
}
#if 0
static QString encode_id(const QStringList &in)
{
	QStringList list = in;
	for(int n = 0; n < list.count(); ++n)
		list[n] = id_part_escape(list[n]);
	return list.join(",");
}
#endif
static QStringList decode_id(const QString &in)
{
	QStringList list = in.split(',');
	for(int n = 0; n < list.count(); ++n)
		list[n] = id_part_unescape(list[n]);
	return list;
}

static QString element_name_for_driver(const QString &driver, PDevice::Type type)
{
	QString element_name;

	if(driver == "alsa")
	{
		if(type == PDevice::AudioOut)
			element_name = "alsasink";
		else if(type == PDevice::AudioIn)
			element_name = "alsasrc";
	}
	else if(driver == "oss")
	{
		if(type == PDevice::AudioOut)
			element_name = "osssink";
		else if(type == PDevice::AudioIn)
			element_name = "osssrc";
	}
	else if(driver == "osxaudio")
	{
		if(type == PDevice::AudioOut)
			element_name = "osxaudiosink";
		else if(type == PDevice::AudioIn)
			element_name = "osxaudiosrc";
	}
	else if(driver == "osxvideo")
	{
		if(type == PDevice::VideoIn)
			element_name = "osxvideosrc";
	}
	else if(driver == "v4l")
	{
		if(type == PDevice::VideoIn)
			element_name = "v4lsrc";
	}
	else if(driver == "v4l2")
	{
		if(type == PDevice::VideoIn)
			element_name = "v4l2src";
	}
	else if(driver == "directsound")
	{
		if(type == PDevice::AudioOut)
			element_name = "directsoundsink";
		else if(type == PDevice::AudioIn)
			element_name = "directsoundsrc";
	}
	else if(driver == "winks")
	{
		if(type == PDevice::VideoIn)
			element_name = "ksvideosrc";
	}

	return element_name;
}
#if 0
// check to see that the necessary sources/sinks are available
static QStringList check_supported_drivers(const QStringList &drivers, PDevice::Type type)
{
	QStringList out;
	foreach(const QString &driver, drivers)
	{
		QString element_name = element_name_for_driver(driver, type);
		if(element_name.isEmpty())
			continue;

		GstElement *e = gst_element_factory_make(element_name.toLatin1().data(), NULL); // why not gst_element_factory_find?
		if(e)
		{
			out += driver;
			g_object_unref(G_OBJECT(e));
		}
	}
	return out;
}
#endif
static GstElement *make_element_with_device(const QString &element_name, const QString &device_id)
{
	GstElement *e = gst_element_factory_make(element_name.toLatin1().data(), NULL);
	if(!e)
		return 0;

	if(!device_id.isEmpty())
	{
		// FIXME: is there a better way to determine if "device" is a string or int?
		if(element_name == "osxaudiosrc" || element_name == "osxaudiosink")
			g_object_set(G_OBJECT(e), "device", device_id.toInt(), NULL);
		else
			g_object_set(G_OBJECT(e), "device", device_id.toLatin1().data(), NULL);
	}
	else
	{
		// FIXME: remove this when ksvideosrc supports enumeration
		if(element_name == "ksvideosrc")
		{
			QByteArray val = qgetenv("PSI_KSVIDEOSRC_INDEX");
			if(!val.isEmpty())
				g_object_set(G_OBJECT(e), "device-index", val.toInt(), NULL);
		}
	}

	return e;
}
#if 0
static bool test_video(const QString &element_name, const QString &device_id)
{
	GstElement *e = make_element_with_device(element_name, device_id);
	if(!e)
		return false;

	gst_element_set_state(e, GST_STATE_PAUSED);
	int ret = gst_element_get_state(e, NULL, NULL, GST_CLOCK_TIME_NONE);

	// 'ret' has our answer, so we can free up the element now
	gst_element_set_state(e, GST_STATE_NULL);
	gst_element_get_state(e, NULL, NULL, GST_CLOCK_TIME_NONE);
	g_object_unref(G_OBJECT(e));

	if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_NO_PREROLL)
		return false;

	return true;
}

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


static QList<GstDevice> devices_for_drivers(const QStringList &drivers, PDevice::Type type)
{
	QList<GstDevice> out;

	QStringList supportedDrivers = check_supported_drivers(drivers, type); // filtered `drivers`
	foreach(const QString &driver, supportedDrivers)
	{
		QString element_name = element_name_for_driver(driver, type);
		if(element_should_use_probe(element_name))
		{
			GstElement *e = gst_element_factory_make(element_name.toLatin1().data(), NULL);
			QList<GstDeviceProbeValue> list = device_probe(e);
			g_object_unref(G_OBJECT(e));

			bool first = true;
			foreach(const GstDeviceProbeValue &i, list)
			{
				GstDevice dev;
				dev.name = i.name;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
				dev.name += QString(" (%1)").arg(driver);
#endif
				dev.isDefault = first;

				QStringList parts;
				parts += driver;
				parts += i.id;
				dev.id = encode_id(parts);

				out += dev;
				first = false;
			}
		}
		else
		{
			QList<DeviceEnum::Item> list = device_enum(driver, type);

			bool first = true;
			foreach(const DeviceEnum::Item &i, list)
			{
				if(type == PDevice::VideoIn && (element_name == "v4lsrc" || element_name == "v4l2src"))
				{
					if(!test_video(element_name, i.id))
						continue;
				}
				else if(element_name == "directsoundsrc" || element_name == "directsoundsink" || element_name == "ksvideosrc" || element_name == "osxvideosrc")
				{
					if(!test_element(element_name))
						continue;
				}

				GstDevice dev;
				dev.name = i.name;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
				dev.name += QString(" (%1)").arg(i.driver);
#endif
				dev.isDefault = first;

				QStringList parts;
				parts += i.driver;
				parts += i.id;
				if(i.explicitCaptureSize.isValid())
					parts += resolution_to_string(i.explicitCaptureSize);
				dev.id = encode_id(parts);

				out += dev;
				first = false;
			}
		}
	}

	return out;
}
#endif

GList *devices_glist(PDevice::Type type)
{
	// below is very stupid usage of device monitor just for compatibility reasons

	GstDeviceMonitor *monitor;
	monitor = gst_device_monitor_new();

	if (type == PDevice::AudioOut) {
		gst_device_monitor_add_filter (monitor, "Audio/Sink", NULL);
	} else if (type == PDevice::AudioIn) {
		gst_device_monitor_add_filter (monitor, "Audio/Source", NULL);
	} else { // VideoIn
		GstCaps *caps;

		caps = gst_caps_new_empty_simple ("video/x-raw");
		gst_device_monitor_add_filter (monitor, "Video/Source", caps);
		caps = gst_caps_new_empty_simple ("image/jpeg");
		gst_device_monitor_add_filter (monitor, "Video/Source", caps);
		gst_caps_unref (caps);
	}

	return gst_device_monitor_get_devices(monitor);
}

QList<GstDevice> devices_list(PDevice::Type type)
{
	QList<GstDevice> ret;
	GList *devs = devices_glist(type);
	GList *dev = devs;
	for (; dev != NULL; dev = dev->next) {
		::GstDevice *gdev = (::GstDevice*)(dev->data);
		PsiMedia::GstDevice d;
		gchar *name = gst_device_get_display_name(gdev);
		d.name = QString::fromUtf8(name);
		d.id = d.name;
		d.isDefault = false; // TODO
		ret.append(d);
		g_free(name);
	}
	g_list_free(devs);
	return ret;

#if 0
	QStringList drivers;
	if(type == PDevice::AudioOut)
	{
		drivers
#if defined(Q_OS_MAC)
		<< "osxaudio"
#elif defined(Q_OS_LINUX)
		<< "alsa"
#else
		<< "oss"
#endif
		<< "directsound";
	}
	else if(type == PDevice::AudioIn)
	{
		drivers
#if defined(Q_OS_MAC)
		<< "osxaudio"
#elif defined(Q_OS_LINUX)
		<< "alsa"
#else
		<< "oss"
#endif
		<< "directsound";
	}
	else // PDevice::VideoIn
	{
		drivers
		<< "v4l"
		<< "v4l2"
		<< "osxvideo"
		<< "winks";
	}

	return devices_for_drivers(drivers, type);
#endif
}

GstElement *devices_makeElement(const QString &id, PDevice::Type type, QSize *captureSize)
{
	GList *devs = devices_glist(type);
	GList *dev = devs;
	for (; dev != NULL; dev = dev->next) {
		gchar *name = gst_device_get_display_name((::GstDevice*)dev->data);
		if (QString::fromUtf8(name) == id) {
			return gst_device_create_element((::GstDevice*)dev->data, NULL);
		}
		g_free(name);
	}
	g_list_free(devs);

#if 0
	QStringList parts = decode_id(id);
	if(parts.count() < 2)
		return 0;

	QString driver = parts[0];
	QString device_id = parts[1];
	QString element_name = element_name_for_driver(driver, type);
	if(element_name.isEmpty())
		return 0;

	GstElement *e = make_element_with_device(element_name, device_id);
	if(!e)
		return 0;

	// FIXME: we don't set v4l2src to the READY state because it may break
	//   the element in jpeg mode.  this is really a bug in gstreamer or
	//   lower that should be fixed...
	if(element_name != "v4l2src")
	{
		gst_element_set_state(e, GST_STATE_READY);
		int ret = gst_element_get_state(e, NULL, NULL, GST_CLOCK_TIME_NONE);
		if(ret != GST_STATE_CHANGE_SUCCESS)
		{
			g_object_unref(G_OBJECT(e));
			return 0;
		}
	}

	if(parts.count() >= 3 && captureSize)
		*captureSize = string_to_resolution(parts[2]);

	return e;
#endif
}

}
