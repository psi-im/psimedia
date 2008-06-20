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

#include "psimedia.h"

#include <QtCore>

namespace PsiMedia {

//----------------------------------------------------------------------------
// Global
//----------------------------------------------------------------------------
PluginResult loadPlugin(const QString &fname, const QString &resourcePath)
{
}

void unloadPlugin()
{
}

QList<AudioParams> supportedAudioModes()
{
	QList<AudioParams> list;
	{
		AudioParams p;
		p.setCodec("speex");
		p.setSampleRate(8000);
		p.setSampleSize(16);
		p.setChannels(1);
		list += p;
	}
	{
		AudioParams p;
		p.setCodec("speex");
		p.setSampleRate(16000);
		p.setSampleSize(16);
		p.setChannels(1);
		list += p;
	}
	{
		AudioParams p;
		p.setCodec("speex");
		p.setSampleRate(32000);
		p.setSampleSize(16);
		p.setChannels(1);
		list += p;
	}
	return list;
}

QList<VideoParams> supportedVideoModes()
{
	QList<VideoParams> list;
	{
		VideoParams p;
		p.setCodec("theora");
		p.setSize(QSize(160, 120));
		p.setFps(15);
		list += p;
	}
	{
		VideoParams p;
		p.setCodec("theora");
		p.setSize(QSize(320, 240));
		p.setFps(15);
		list += p;
	}
	{
		VideoParams p;
		p.setCodec("theora");
		p.setSize(QSize(320, 240));
		p.setFps(30);
		list += p;
	}
	return list;
}

class Device::Private
{
public:
	Device::Type type;
	QString id;
	QString name;
};

class Global
{
public:
	static Device makeDevice(Device::Type type, const QString &id, const QString &name)
	{
		Device dev;
		dev.d = new Device::Private;
		dev.d->type = type;
		dev.d->id = id;
		dev.d->name = name;
		return dev;
	}
};

// some fake devices for now
QList<Device> audioOutputDevices()
{
	QList<Device> list;
	{
		list += Global::makeDevice(Device::AudioOut, "default", "STAC92xx Analog");
	}
	return list;
}

QList<Device> audioInputDevices()
{
	QList<Device> list;
	{
		list += Global::makeDevice(Device::AudioIn, "default", "USB Audio");
	}
	return list;
}

QList<Device> videoInputDevices()
{
	QList<Device> list;
	{
		list += Global::makeDevice(Device::VideoIn, "default", "Laptop Integrated Webcam");
	}
	return list;
}

//----------------------------------------------------------------------------
// Device
//----------------------------------------------------------------------------
Device::Device() :
	d(0)
{
}

Device::Device(const Device &other) :
	d(other.d ? new Private(*other.d) : 0)
{
}

Device::~Device()
{
	delete d;
}

Device & Device::operator=(const Device &other)
{
	if(d)
	{
		if(other.d)
		{
			*d = *other.d;
		}
		else
		{
			delete d;
			d = 0;
		}
	}
	else
	{
		if(other.d)
			d = new Private(*other.d);
	}

	return *this;
}

bool Device::isNull() const
{
	return (d ? false : true);
}

Device::Type Device::type() const
{
	return d->type;
}

QString Device::name() const
{
	return d->name;
}

QString Device::id() const
{
	return d->id;
}

#ifdef QT_GUI_LIB
//----------------------------------------------------------------------------
// VideoWidget
//----------------------------------------------------------------------------
VideoWidget::VideoWidget(QWidget *parent)
{
	QPalette palette;
	palette.setColor(backgroundRole(), Qt::black);
	setPalette(palette);

	setAutoFillBackground(true);
}

VideoWidget::~VideoWidget()
{
}
#endif

//----------------------------------------------------------------------------
// AudioParams
//----------------------------------------------------------------------------
class AudioParams::Private
{
public:
	QString codec;
	int sampleRate;
	int sampleSize;
	int channels;

	Private() :
		sampleRate(0),
		sampleSize(0),
		channels(0)
	{
	}
};

AudioParams::AudioParams() :
	d(new Private)
{
}

AudioParams::AudioParams(const AudioParams &other) :
	d(new Private(*other.d))
{
}

AudioParams::~AudioParams()
{
	delete d;
}

AudioParams & AudioParams::operator=(const AudioParams &other)
{
	*d = *other.d;
	return *this;
}

QString AudioParams::codec() const
{
	return d->codec;
}

int AudioParams::sampleRate() const
{
	return d->sampleRate;
}

int AudioParams::sampleSize() const
{
	return d->sampleSize;
}

int AudioParams::channels() const
{
	return d->channels;
}

void AudioParams::setCodec(const QString &s)
{
	d->codec = s;
}

void AudioParams::setSampleRate(int n)
{
	d->sampleRate = n;
}

void AudioParams::setSampleSize(int n)
{
	d->sampleSize = n;
}

void AudioParams::setChannels(int n)
{
	d->channels = n;
}

bool AudioParams::operator==(const AudioParams &other) const
{
	if(d->codec == other.d->codec &&
		d->sampleRate == other.d->sampleRate &&
		d->sampleSize == other.d->sampleSize &&
		d->channels == other.d->channels)
	{
		return true;
	}
	else
		return false;
}

//----------------------------------------------------------------------------
// VideoParams
//----------------------------------------------------------------------------
class VideoParams::Private
{
public:
	QString codec;
	QSize size;
	int fps;

	Private() :
		fps(0)
	{
	}
};

VideoParams::VideoParams() :
	d(new Private)
{
}

VideoParams::VideoParams(const VideoParams &other) :
	d(new Private(*other.d))
{
}

VideoParams::~VideoParams()
{
	delete d;
}

VideoParams & VideoParams::operator=(const VideoParams &other)
{
	*d = *other.d;
	return *this;
}

QString VideoParams::codec() const
{
	return d->codec;
}

QSize VideoParams::size() const
{
	return d->size;
}

int VideoParams::fps() const
{
	return d->fps;
}

void VideoParams::setCodec(const QString &s)
{
	d->codec = s;
}

void VideoParams::setSize(const QSize &s)
{
	d->size = s;
}

void VideoParams::setFps(int n)
{
	d->fps = n;
}

bool VideoParams::operator==(const VideoParams &other) const
{
	if(d->codec == other.d->codec &&
		d->size == other.d->size &&
		d->fps == other.d->fps)
	{
		return true;
	}
	else
		return false;
}

//----------------------------------------------------------------------------
// RtpPacket
//----------------------------------------------------------------------------
class RtpPacket::Private : public QSharedData
{
public:
	QByteArray rawValue;
	int portOffset;

	Private(const QByteArray &_rawValue, int _portOffset) :
		rawValue(_rawValue),
		portOffset(_portOffset)
	{
	}
};

RtpPacket::RtpPacket() :
	d(0)
{
}

RtpPacket::RtpPacket(const QByteArray &rawValue, int portOffset) :
	d(new Private(rawValue, portOffset))
{
}

RtpPacket::RtpPacket(const RtpPacket &other) :
	d(other.d)
{
}

RtpPacket::~RtpPacket()
{
}

RtpPacket & RtpPacket::operator=(const RtpPacket &other)
{
	d = other.d;
	return *this;
}

bool RtpPacket::isNull() const
{
	return (d ? false : true);
}

QByteArray RtpPacket::rawValue() const
{
	return d->rawValue;
}

int RtpPacket::portOffset() const
{
	return d->portOffset;
}

//----------------------------------------------------------------------------
// RtpSource
//----------------------------------------------------------------------------
RtpSource::RtpSource()
{
}

RtpSource::~RtpSource()
{
}

int RtpSource::packetsAvailable() const
{
}

RtpPacket RtpSource::read()
{
}

void RtpSource::connectNotify(const char *signal)
{
}

void RtpSource::disconnectNotify(const char *signal)
{
}

//----------------------------------------------------------------------------
// RtpSink
//----------------------------------------------------------------------------
RtpSink::RtpSink()
{
}

RtpSink::~RtpSink()
{
}

void RtpSink::write(const RtpPacket &rtp)
{
}

//----------------------------------------------------------------------------
// Recorder
//----------------------------------------------------------------------------
Recorder::Recorder(QObject *parent)
{
}

Recorder::~Recorder()
{
}

QIODevice *Recorder::device() const
{
}

void Recorder::setDevice(QIODevice *dev)
{
}

//----------------------------------------------------------------------------
// PayloadInfo
//----------------------------------------------------------------------------
PayloadInfo::PayloadInfo()
{
}

PayloadInfo::PayloadInfo(const PayloadInfo &other)
{
}

PayloadInfo::~PayloadInfo()
{
}

PayloadInfo & PayloadInfo::operator=(const PayloadInfo &other)
{
}

bool PayloadInfo::isNull() const
{
}

int PayloadInfo::id() const
{
}

QString PayloadInfo::name() const
{
}

int PayloadInfo::clockrate() const
{
}

int PayloadInfo::channels() const
{
}

int PayloadInfo::ptime() const
{
}

int PayloadInfo::maxptime() const
{
}

QList<PayloadInfo::Parameter> PayloadInfo::parameters() const
{
}

void PayloadInfo::setId(int i)
{
}

void PayloadInfo::setName(const QString &str)
{
}

void PayloadInfo::setClockrate(int i)
{
}

void PayloadInfo::setChannels(int num)
{
}

void PayloadInfo::setPtime(int i)
{
}

void PayloadInfo::setMaxptime(int i)
{
}

void PayloadInfo::setParameters(const QList<PayloadInfo::Parameter> &params)
{
}

bool PayloadInfo::operator==(const PayloadInfo &other) const
{
}

//----------------------------------------------------------------------------
// Receiver
//----------------------------------------------------------------------------
Receiver::Receiver(QObject *parent)
{
}

Receiver::~Receiver()
{
}

void Receiver::setAudioOutputDevice(const QString &deviceId)
{
}

#ifdef QT_GUI_LIB
void Receiver::setVideoWidget(VideoWidget *widget)
{
}
#endif

void Receiver::setRecorder(Recorder *recorder)
{
}

void Receiver::setAudioPayloadInfo(const QList<PayloadInfo> &info)
{
}

void Receiver::setVideoPayloadInfo(const QList<PayloadInfo> &info)
{
}

void Receiver::setAudioParams(const QList<AudioParams> &params)
{
}

void Receiver::setVideoParams(const QList<VideoParams> &params)
{
}

void Receiver::start()
{
	QTimer::singleShot(1000, this, SIGNAL(started()));
}

void Receiver::stop()
{
	QTimer::singleShot(1000, this, SIGNAL(stopped()));
}

QList<PayloadInfo> Receiver::audioPayloadInfo() const
{
}

QList<PayloadInfo> Receiver::videoPayloadInfo() const
{
}

QList<AudioParams> Receiver::audioParams() const
{
}

QList<VideoParams> Receiver::videoParams() const
{
}

int Receiver::volume() const
{
}

void Receiver::setVolume(int level)
{
}

Receiver::Error Receiver::errorCode() const
{
}

RtpSink *Receiver::audioRtpSink()
{
}

RtpSink *Receiver::videoRtpSink()
{
}

//----------------------------------------------------------------------------
// Producer
//----------------------------------------------------------------------------
Producer::Producer(QObject *parent)
{
}

Producer::~Producer()
{
}

void Producer::setAudioInputDevice(const QString &deviceId)
{
}

void Producer::setVideoInputDevice(const QString &deviceId)
{
}

void Producer::setFileInput(const QString &fileName)
{
}

void Producer::setFileDataInput(const QByteArray &fileData)
{
}

#ifdef QT_GUI_LIB
void Producer::setVideoWidget(VideoWidget *widget)
{
}
#endif

void Producer::setAudioPayloadInfo(const QList<PayloadInfo> &info)
{
}

void Producer::setVideoPayloadInfo(const QList<PayloadInfo> &info)
{
}

void Producer::setAudioParams(const QList<AudioParams> &params)
{
}

void Producer::setVideoParams(const QList<VideoParams> &params)
{
}

void Producer::start()
{
	QTimer::singleShot(1000, this, SIGNAL(started()));
}

void Producer::transmitAudio(int paramsIndex)
{
}

void Producer::transmitVideo(int paramsIndex)
{
}

void Producer::pauseAudio()
{
}

void Producer::pauseVideo()
{
}

void Producer::stop()
{
	QTimer::singleShot(1000, this, SIGNAL(stopped()));
}

QList<PayloadInfo> Producer::audioPayloadInfo() const
{
}

QList<PayloadInfo> Producer::videoPayloadInfo() const
{
}

QList<AudioParams> Producer::audioParams() const
{
}

QList<VideoParams> Producer::videoParams() const
{
}

int Producer::volume() const
{
}

void Producer::setVolume(int level)
{
}

Producer::Error Producer::errorCode() const
{
}

RtpSource *Producer::audioRtpSource()
{
}

RtpSource *Producer::videoRtpSource()
{
}

}
