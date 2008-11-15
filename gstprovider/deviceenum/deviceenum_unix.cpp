/*
 * Copyright (C) 2006  Justin Karneges
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "deviceenum.h"

#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#ifdef Q_OS_LINUX
# include <sys/stat.h>
# include <dirent.h>
# include <sys/ioctl.h>
# include <linux/videodev.h>
#endif

namespace DeviceEnum {

#define DIR_INPUT  1
#define DIR_OUTPUT 2

// taken from netinterface_unix (changed the split to KeepEmptyParts)
static QStringList read_proc_as_lines(const char *procfile)
{
	QStringList out;

	FILE *f = fopen(procfile, "r");
	if(!f)
		return out;

	QByteArray buf;
	while(!feof(f))
	{
		// max read on a proc is 4K
		QByteArray block(4096, 0);
		int ret = fread(block.data(), 1, block.size(), f);
		if(ret <= 0)
			break;
		block.resize(ret);
		buf += block;
	}
	fclose(f);

	QString str = QString::fromLocal8Bit(buf);
	out = str.split('\n', QString::KeepEmptyParts);
	return out;
}

// check scheme from portaudio
static bool check_oss(const QString &dev, bool input)
{
	int fd = open(QFile::encodeName(dev).data(), (input ? O_RDONLY : O_WRONLY) | O_NONBLOCK);
	if(fd == -1)
	{
		if(errno == EBUSY || errno == EAGAIN)
			return false; // device is busy
		else
			return false; // can't access
	}
	close(fd);
	return true;
}

static QList<Item> get_oss_items(int type)
{
	QList<Item> out;

	// sndstat detection scheme from pulseaudio
	QStringList stat;
	stat = read_proc_as_lines("/dev/sndstat");
	if(stat.isEmpty())
	{
		stat = read_proc_as_lines("/proc/sndstat");
		if(stat.isEmpty())
		{
			stat = read_proc_as_lines("/proc/asound/oss/sndstat");
			if(stat.isEmpty())
				return out;
		}
	}

	// sndstat processing scheme from pulseaudio
	int at;
	at = stat.indexOf("Audio devices:");
	if(at == -1)
	{
		at = stat.indexOf("Installed devices:");
		if(at == -1)
			return out;
	}
	++at;

	for(; at < stat.count() && !stat[at].isEmpty(); ++at)
	{
		QString line = stat[at];
		int x = line.indexOf(": ");
		if(x == -1)
			continue;

		QString devnum = line.mid(0, x);
		QString devname = line.mid(x + 2);

		// apparently FreeBSD ids start with pcm in front
		bool bsd = false;
		if(devnum.left(3) == "pcm")
		{
			bsd = true;
			devnum = devnum.mid(3);
		}

		bool ok;
		int num = devnum.toInt(&ok);
		if(!ok)
			continue;

		x = devname.indexOf(" (DUPLEX)");
		if(x != -1)
			devname = devname.mid(0, x);

		QStringList possible;
		// apparently FreeBSD has ".0" appended to the devices
		if(bsd)
			possible += QString("/dev/dsp%1.0").arg(num);
		else
			possible += QString("/dev/dsp%1").arg(num);

		// if we're looking for the 0 item, this might be "dsp"
		//   without a number on it
		if(num == 0 && !bsd)
			possible += "/dev/dsp";

		QString dev;
		foreach(dev, possible)
		{
			if(QFile::exists(dev))
				break;
		}

		if(type & DIR_INPUT && check_oss(dev, true))
		{
			Item i;
			i.type = Item::Audio;
			i.dir = Item::Input;
			i.name = devname;
			i.driver = "oss";
			i.id = dev;
			out += i;
		}

		if(type & DIR_OUTPUT && check_oss(dev, false))
		{
			Item i;
			i.type = Item::Audio;
			i.dir = Item::Output;
			i.name = devname;
			i.driver = "oss";
			i.id = dev;
			out += i;
		}
	}

	return out;
}

// /proc/asound/devices
//   16: [0- 0]: digital audio playback
//   24: [0- 0]: digital audio capture
//    0: [0- 0]: ctl
//   33:       : timer
//   56: [1- 0]: digital audio capture
//   32: [1- 0]: ctl
//
// /proc/asound/pcm
//   00-00: ALC260 Analog : ALC260 Analog : playback 1 : capture 1
//   01-00: USB Audio : USB Audio : capture 1
class AlsaItem
{
public:
	int card, dev;
	bool input;
	QString name;
};

static QList<Item> get_alsa_items(int type)
{
#ifdef Q_OS_LINUX
	QList<Item> out;

	QList<AlsaItem> items;
	QStringList devices_lines = read_proc_as_lines("/proc/asound/devices");
	foreach(QString line, devices_lines)
	{
		// get the fields we care about
		QString devbracket, devtype;
		int x = line.indexOf(": ");
		if(x == -1)
			continue;
		QString sub = line.mid(x + 2);
		x = sub.indexOf(": ");
		if(x == -1)
			continue;
		devbracket = sub.mid(0, x);
		devtype = sub.mid(x + 2);

		// skip all but playback and capture
		bool input;
		if(devtype == "digital audio playback")
			input = false;
		else if(devtype == "digital audio capture")
			input = true;
		else
			continue;

		// skip what isn't asked for
		if(!(type & DIR_INPUT) && input)
			continue;
		if(!(type & DIR_OUTPUT) && !input)
			continue;

		// hack off brackets
		if(devbracket[0] != '[' || devbracket[devbracket.length()-1] != ']')
			continue;
		devbracket = devbracket.mid(1, devbracket.length() - 2);

		QString cardstr, devstr;
		x = devbracket.indexOf('-');
		if(x == -1)
			continue;
		cardstr = devbracket.mid(0, x);
		devstr = devbracket.mid(x + 1);

		AlsaItem ai;
		bool ok;
		ai.card = cardstr.toInt(&ok);
		if(!ok)
			continue;
		ai.dev = devstr.toInt(&ok);
		if(!ok)
			continue;
		ai.input = input;
		ai.name.sprintf("ALSA Card %d, Device %d", ai.card, ai.dev);
		items += ai;
	}

	// try to get the friendly names
	QStringList pcm_lines = read_proc_as_lines("/proc/asound/pcm");
	foreach(QString line, pcm_lines)
	{
		QString devnumbers, devname;
		int x = line.indexOf(": ");
		if(x == -1)
			continue;
		devnumbers = line.mid(0, x);
		devname = line.mid(x + 2);
		x = devname.indexOf(" :");
		if(x != -1)
			devname = devname.mid(0, x);
		else
			devname = devname.trimmed();

		QString cardstr, devstr;
		x = devnumbers.indexOf('-');
		if(x == -1)
			continue;
		cardstr = devnumbers.mid(0, x);
		devstr = devnumbers.mid(x + 1);

		bool ok;
		int cardnum = cardstr.toInt(&ok);
		if(!ok)
			continue;
		int devnum = devstr.toInt(&ok);
		if(!ok)
			continue;

		for(int n = 0; n < items.count(); ++n)
		{
			AlsaItem &ai = items[n];
			if(ai.card == cardnum && ai.dev == devnum)
				ai.name = devname;
		}
	}

	// make a "default" item
	{
		Item i;
		i.type = Item::Audio;
		if(type == DIR_INPUT)
			i.dir = Item::Input;
		else // DIR_OUTPUT
			i.dir = Item::Output;
		i.name = "Default";
		i.driver = "alsa";
		i.id = "default";
		out += i;
	}

	for(int n = 0; n < items.count(); ++n)
	{
		AlsaItem &ai = items[n];

		// make an item for both hw and plughw
		Item i;
		i.type = Item::Audio;
		if(ai.input)
			i.dir = Item::Input;
		else
			i.dir = Item::Output;
		i.name = ai.name;
		i.driver = "alsa";
		i.id = QString().sprintf("plughw:%d,%d", ai.card, ai.dev);
		out += i;

		// internet discussion seems to indicate that plughw is the
		//   same as hw except that it will convert audio parameters
		//   if necessary.  the decision to use hw vs plughw is a
		//   development choice, NOT a user choice.  it is generally
		//   recommended for apps to use plughw unless they have a
		//   good reason.
		//
		// so, for now we'll only offer plughw and not hw
		//i.name = ai.name + " (Direct)";
		//i.id = QString().sprintf("hw:%d,%d", ai.card, ai.dev);
		//out += i;
	}

	return out;
#else
	// return empty list if non-linux
	Q_UNUSED(type);
	return QList<Item>();
#endif
}

#ifdef Q_OS_LINUX
static QStringList scan_for_videodevs(const QString &dirpath)
{
	QStringList out;
	DIR *dir = opendir(QFile::encodeName(dirpath));
	if(!dir)
		return out;
	while(1)
	{
		struct dirent *e;
		e = readdir(dir);
		if(!e)
			break;
		QString fname = QFile::decodeName(e->d_name);
		if(fname == "." || fname == "..")
			continue;
		QFileInfo fi(dirpath + '/' + fname);
		if(fi.isSymLink())
			continue;

		if(fi.isDir())
		{
			out += scan_for_videodevs(fi.filePath());
		}
		else
		{
			struct stat buf;
			if(lstat(QFile::encodeName(fi.filePath()).data(), &buf) == -1)
				continue;
			if(!S_ISCHR(buf.st_mode))
				continue;
			int maj = ((unsigned short)buf.st_rdev) >> 8;
			int min = ((unsigned short)buf.st_rdev) & 0xff;
			if(maj == 81 && (min >= 0 && min <= 63))
				out += fi.filePath();
		}
	}
	closedir(dir);
	return out;
}
#endif

class V4LName
{
public:
	QString name;
	QString dev;
	QString friendlyName;
};

static QList<V4LName> get_v4l_names(const QString &path, bool sys)
{
	QList<V4LName> out;
	QDir dir(path);
	if(!dir.exists())
		return out;
	QStringList entries = dir.entryList();
	foreach(QString fname, entries)
	{
		QFileInfo fi(dir.filePath(fname));
		if(sys)
		{
			// sys names are dirs
			if(!fi.isDir())
				continue;

			// sys names should begin with "video"
			if(fname.left(5) != "video")
				continue;

			V4LName v;
			v.name = fname;
			v.dev = QString("/dev/%1").arg(fname);

			QString modelPath = fi.filePath() + "/model";
			QStringList lines = read_proc_as_lines(QFile::encodeName(modelPath).data());
			if(!lines.isEmpty())
				v.friendlyName = lines.first();

			out += v;
		}
		else
		{
			// proc names are not dirs
			if(fi.isDir())
				continue;

			// proc names need to be split into name/number
			int at;
			for(at = fname.length() - 1; at >= 0; --at)
			{
				if(!fname[at].isDigit())
					break;
			}
			++at;
			QString numstr = fname.mid(at);
			QString base = fname.mid(0, at);
			bool ok;
			int num = numstr.toInt(&ok);
			if(!ok)
				continue;

			// name should be "video" or "capture"
			if(base != "video" || base != "capture")
				continue;

			// but apparently the device is always called "video"
			QString dev = QString("/dev/video%1").arg(num);

			V4LName v;
			v.name = fname;
			v.dev = dev;
			out += v;
		}
	}
	return out;
}

// v4l detection scheme adapted from PWLib (used by Ekiga/Gnomemeeting)
static QList<Item> get_v4l_items()
{
#ifdef Q_OS_LINUX
	QList<Item> out;

	QList<V4LName> list = get_v4l_names("/sys/class/video4linux", true);
	if(list.isEmpty())
		list = get_v4l_names("/proc/video/dev", false);

	// if we can't find anything, then do a raw scan for possibilities
	if(list.isEmpty())
	{
		QStringList possible = scan_for_videodevs("/dev");
		foreach(QString str, possible)
		{
			V4LName v;
			v.dev = str;
			list += v;
		}
	}

	for(int n = 0; n < list.count(); ++n)
	{
		V4LName &v = list[n];

		// if we already have a friendly name then we'll skip the confirm
		//   in order to save resources.  the only real drawback here that
		//   I can think of is if the device isn't a capture type.  but
		//   what does it mean to have a V4L device that isn't capture??
		if(v.friendlyName.isEmpty())
		{
			int fd = open(QFile::encodeName(v.dev).data(), O_RDONLY | O_NONBLOCK);
			if(fd == -1)
				continue;

			// get video capabilities and close
			struct video_capability caps;
			memset(&caps, 0, sizeof(caps));
			int ret = ioctl(fd, VIDIOCGCAP, &caps);
			close(fd);
			if(ret == -1)
				continue;

			if(!(caps.type & VID_TYPE_CAPTURE))
				continue;

			v.friendlyName = caps.name;
		}

		Item i;
		i.type = Item::Video;
		i.dir = Item::Input;
		i.name = v.friendlyName;
		i.driver = "v4l";
		i.id = v.dev;

		// HACK
		if(v.friendlyName == "Labtec Webcam Notebook")
			i.explicitCaptureSize = QSize(640, 480);

		out += i;
	}

	return out;
#else
	// return empty list if non-linux
	return QList<Item>();
#endif
}

static QList<Item> get_v4l2_items()
{
#ifdef Q_OS_LINUX
	QList<Item> out;

	QList<V4LName> list = get_v4l_names("/sys/class/video4linux", true);
	if(list.isEmpty())
		list = get_v4l_names("/proc/video/dev", false);

	// if we can't find anything, then do a raw scan for possibilities
	if(list.isEmpty())
	{
		QStringList possible = scan_for_videodevs("/dev");
		foreach(QString str, possible)
		{
			V4LName v;
			v.dev = str;
			list += v;
		}
	}

	for(int n = 0; n < list.count(); ++n)
	{
		V4LName &v = list[n];

		// if we already have a friendly name then we'll skip the confirm
		//   in order to save resources.  the only real drawback here that
		//   I can think of is if the device isn't a capture type.  but
		//   what does it mean to have a V4L device that isn't capture??
		if(v.friendlyName.isEmpty())
		{
			int fd = open(QFile::encodeName(v.dev).data(), O_RDONLY | O_NONBLOCK);
			if(fd == -1)
				continue;

			// get video capabilities and close
			struct v4l2_capability caps;
			memset(&caps, 0, sizeof(caps));
			int ret = ioctl(fd, VIDIOC_QUERYCAP, &caps);
			close(fd);
			if(ret == -1)
				continue;

			if(!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE))
				continue;

			v.friendlyName = (const char *)caps.card;
		}

		Item i;
		i.type = Item::Video;
		i.dir = Item::Input;
		i.name = v.friendlyName;
		i.driver = "v4l2";
		i.id = v.dev;
		out += i;
	}

	return out;
#else
	// return empty list if non-linux
	return QList<Item>();
#endif
}

QList<Item> audioOutputItems(const QString &driver)
{
	QList<Item> out;
	if(driver.isEmpty() || driver == "oss")
		out += get_oss_items(DIR_OUTPUT);
	if(driver.isEmpty() || driver == "alsa")
		out += get_alsa_items(DIR_OUTPUT);
	return out;
}

QList<Item> audioInputItems(const QString &driver)
{
	QList<Item> out;
	if(driver.isEmpty() || driver == "oss")
		out += get_oss_items(DIR_INPUT);
	if(driver.isEmpty() || driver == "alsa")
		out += get_alsa_items(DIR_INPUT);
	return out;
}

QList<Item> videoInputItems(const QString &driver)
{
	QList<Item> out;
	if(driver.isEmpty() || driver == "v4l2")
		out += get_v4l2_items();
	if(driver.isEmpty() || driver == "v4l")
		out += get_v4l_items();
	return out;
}

}
