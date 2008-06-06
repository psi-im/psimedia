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

#ifndef PSIMEDIA_H
#define PSIMEDIA_H

#ifdef QT_GUI_LIB
#include <QWidget>
#include <QStringList>
#include <QSharedDataPointer>
#endif

namespace PsiMedia {

enum PluginResult
{
	Success,
	ErrorLoad,
	ErrorVersion,
	ErrorInit
};

PluginResult loadPlugin(const QString &fname, const QString &resourcePath);
void unloadPlugin();

// at minimum, will always contain "speex", "vorbis", and "theora"
QStringList supportedCodecs();

class Device
{
public:
	enum Type
	{
		AudioIn,  // microphone
		AudioOut, // speaker
		VideoIn   // camera
	};

	Device();
	Device(const Device &other);
	~Device();
	Device & operator=(const Device &other);

	bool isNull() const;
	Type type() const;
	QString name() const;
	QString id() const;

private:
	class Private;
	Private *d;
};

#ifdef QT_GUI_LIB
class VideoWidget : public QWidget
{
	Q_OBJECT

public:
	VideoWidget(QWidget *parent = 0);
	~VideoWidget();

private:
	Q_DISABLE_COPY(VideoWidget);

	class Private;
	Private *d;
};
#endif

class AudioParams
{
public:
	AudioParams();
	AudioParams(const AudioParams &other);
	~AudioParams();
	AudioParams & operator=(const AudioParams &other);

	QString codec() const;
	int sampleRate() const;
	int sampleSize() const;
	int channels() const;

	void setCodec(const QString &s);
	void setSampleRate(int n);
	void setSampleSize(int n);
	void setChannels(int n);

private:
	class Private;
	Private *d;
};

class VideoParams
{
public:
	VideoParams();
	VideoParams(const VideoParams &other);
	~VideoParams();
	VideoParams & operator=(const VideoParams &other);

	QString codec() const;
	QSize size() const;
	int fps() const;

	void setCodec(const QString &s);
	void setSize(const QSize &s);
	void setFps(int n);

private:
	class Private;
	Private *d;
};

class RtpPacket
{
public:
	RtpPacket();
	RtpPacket(const QByteArray &rawValue, int portOffset);
	RtpPacket(const RtpPacket &other);
	~RtpPacket();
	RtpPacket & operator=(const RtpPacket &other);

	bool isNull() const;

	QByteArray rawValue() const;
	int portOffset() const;

private:
	class Private;
	QSharedDataPointer<Private> d;
};

class RtpSource : public QObject
{
	Q_OBJECT

public:
	int packetsAvailable() const;
	RtpPacket read();

signals:
	void readyRead();

private:
	RtpSource();
	~RtpSource();
	Q_DISABLE_COPY(RtpSource);

	class Private;
	friend class Private;
	Private *d;
};

class RtpSink : public QObject
{
	Q_OBJECT

public:
	void write(const RtpPacket &rtp);

signals:
	void packetsWritten(int count);

private:
	RtpSink();
	~RtpSink();
	Q_DISABLE_COPY(RtpSink);

	class Private;
	friend class Private;
	Private *d;
};

// records in ogg theora+vorbis format
class Recorder : public QObject
{
	Q_OBJECT

public:
	Recorder(QObject *parent = 0);
	~Recorder();

	QIODevice *device() const;
	void setDevice(QIODevice *dev);

private:
	Q_DISABLE_COPY(Recorder);

	class Private;
	Private *d;
};

class CodecConfiguration
{
public:
	CodecConfiguration();
	CodecConfiguration(const QByteArray &rawValue);
	CodecConfiguration(const CodecConfiguration &other);
	~CodecConfiguration();
	CodecConfiguration & operator=(const CodecConfiguration &other);

	QByteArray rawValue() const;

private:
	class Private;
	Private *d;
};

class Receiver : public QObject
{
	Q_OBJECT

public:
	enum Error
	{
		ErrorGeneric,
		ErrorSystem,
		ErrorCodec
	};

	Receiver(QObject *parent = 0);
	~Receiver();

	void setAudioOutputDevice(const Device &dev);
#ifdef QT_GUI_LIB
	void setVideoWidget(VideoWidget *widget);
#endif
	void setRecorder(Recorder *recorder);

	void start(const CodecConfiguration &config);
	void stop();

	bool hasAudio() const;
	bool hasVideo() const;
	AudioParams audioParams() const;
	VideoParams videoParams() const;

	int volume() const; // 0 (mute) to 100
	void setVolume(int level);

	Error errorCode() const;

	RtpSink *rtpSink();

signals:
	void started();
	void stopped();
	void paramsChanged();
	void error();

private:
	Q_DISABLE_COPY(Receiver);

	class Private;
	friend class Private;
	Private *d;
};

class Producer : public QObject
{
	Q_OBJECT

public:
	enum Error
	{
		ErrorGeneric
	};

	Producer(QObject *parent = 0);
	~Producer();

	void setAudioInputDevice(const Device &dev);
	void setVideoInputDevice(const Device &dev);
	void setFileInput(const QString &fileName);
	void setFileDataInput(const QByteArray &fileData);
#ifdef QT_GUI_LIB
	void setVideoWidget(VideoWidget *widget);
#endif

	void setAudioParams(const AudioParams &params);
	void setVideoParams(const VideoParams &params);

	void start();
	void startPreviewOnly();
	void stop();

	CodecConfiguration codecConfiguration() const;

	int volume() const; // 0 (mute) to 100
	void setVolume(int level);

	Error errorCode() const;

	RtpSource *rtpSource();

signals:
	void started();
	void stopped();
	void error();

private:
	Q_DISABLE_COPY(Producer);

	class Private;
	friend class Private;
	Private *d;
};

}

#endif
