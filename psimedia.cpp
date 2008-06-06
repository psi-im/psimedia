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

QStringList supportedCodecs()
{
}

//----------------------------------------------------------------------------
// Device
//----------------------------------------------------------------------------
Device::Device()
{
}

Device::Device(const Device &other)
{
}

Device::~Device()
{
}

Device & Device::operator=(const Device &other)
{
}

bool Device::isNull() const
{
}

Device::Type Device::type() const
{
}

QString Device::name() const
{
}

QString Device::id() const
{
}

#ifdef QT_GUI_LIB
//----------------------------------------------------------------------------
// VideoWidget
//----------------------------------------------------------------------------
VideoWidget::VideoWidget(QWidget *parent)
{
}

VideoWidget::~VideoWidget()
{
}
#endif

//----------------------------------------------------------------------------
// AudioParams
//----------------------------------------------------------------------------
AudioParams::AudioParams()
{
}

AudioParams::AudioParams(const AudioParams &other)
{
}

AudioParams::~AudioParams()
{
}

AudioParams & AudioParams::operator=(const AudioParams &other)
{
}

QString AudioParams::codec() const
{
}

int AudioParams::sampleRate() const
{
}

int AudioParams::sampleSize() const
{
}

int AudioParams::channels() const
{
}

void AudioParams::setCodec(const QString &s)
{
}

void AudioParams::setSampleRate(int n)
{
}

void AudioParams::setSampleSize(int n)
{
}

void AudioParams::setChannels(int n)
{
}

//----------------------------------------------------------------------------
// VideoParams
//----------------------------------------------------------------------------
VideoParams::VideoParams()
{
}

VideoParams::VideoParams(const VideoParams &other)
{
}

VideoParams::~VideoParams()
{
}

VideoParams & VideoParams::operator=(const VideoParams &other)
{
}

QString VideoParams::codec() const
{
}

QSize VideoParams::size() const
{
}

int VideoParams::fps() const
{
}

void VideoParams::setCodec(const QString &s)
{
}

void VideoParams::setSize(const QSize &s)
{
}

void VideoParams::setFps(int n)
{
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
// CodecConfiguration
//----------------------------------------------------------------------------
CodecConfiguration::CodecConfiguration()
{
}

CodecConfiguration::CodecConfiguration(const QByteArray &rawValue)
{
}

CodecConfiguration::CodecConfiguration(const CodecConfiguration &other)
{
}

CodecConfiguration::~CodecConfiguration()
{
}

CodecConfiguration & CodecConfiguration::operator=(const CodecConfiguration &other)
{
}

QByteArray CodecConfiguration::rawValue() const
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

void Receiver::setAudioOutputDevice(const Device &dev)
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

void Receiver::start(const CodecConfiguration &config)
{
}

void Receiver::stop()
{
}

bool Receiver::hasAudio() const
{
}

bool Receiver::hasVideo() const
{
}

AudioParams Receiver::audioParams() const
{
}

VideoParams Receiver::videoParams() const
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

RtpSink *Receiver::rtpSink()
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

void Producer::setAudioInputDevice(const Device &dev)
{
}

void Producer::setVideoInputDevice(const Device &dev)
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

void Producer::setAudioParams(const AudioParams &params)
{
}

void Producer::setVideoParams(const VideoParams &params)
{
}

void Producer::start()
{
}

void Producer::startPreviewOnly()
{
}

void Producer::stop()
{
}

CodecConfiguration Producer::codecConfiguration() const
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

RtpSource *Producer::rtpSource()
{
}

}
