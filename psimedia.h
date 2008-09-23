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

#include <QSize>
#include <QStringList>
#include <QSharedDataPointer>

#ifdef QT_GUI_LIB
#include <QWidget>
#endif

namespace PsiMedia {

class Producer;
class ProducerPrivate;
class Receiver;
class ReceiverPrivate;
class VideoWidgetPrivate;
class RtpChannelPrivate;

enum PluginResult
{
	PluginSuccess,
	ErrorLoad,
	ErrorVersion,
	ErrorInit
};

bool isSupported();
PluginResult loadPlugin(const QString &fname, const QString &resourcePath);
void unloadPlugin();
QString creditName();
QString creditText();

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
	friend class Global;
	Private *d;
};

#ifdef QT_GUI_LIB
class VideoWidget : public QWidget
{
	Q_OBJECT

public:
	VideoWidget(QWidget *parent = 0);
	~VideoWidget();

protected:
	virtual void paintEvent(QPaintEvent *event);

private:
	Q_DISABLE_COPY(VideoWidget);

	friend class VideoWidgetPrivate;
	friend class Receiver;
	friend class Producer;
	VideoWidgetPrivate *d;
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

	bool operator==(const AudioParams &other) const;

	inline bool operator!=(const AudioParams &other) const
	{
		return !(*this == other);
	}

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

	bool operator==(const VideoParams &other) const;

	inline bool operator!=(const VideoParams &other) const
	{
		return !(*this == other);
	}

private:
	class Private;
	Private *d;
};

QList<AudioParams> supportedAudioModes();
QList<VideoParams> supportedVideoModes();
QList<Device> audioOutputDevices();
QList<Device> audioInputDevices();
QList<Device> videoInputDevices();

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

// may drop packets if not read fast enough.
// may queue no packets at all, if nobody is listening to readyRead.
class RtpChannel : public QObject
{
	Q_OBJECT

public:
	int packetsAvailable() const;
	RtpPacket read();
	void write(const RtpPacket &rtp);

signals:
	void readyRead();
	void packetsWritten(int count);

protected:
	virtual void connectNotify(const char *signal);
	virtual void disconnectNotify(const char *signal);

private:
	RtpChannel();
	~RtpChannel();
	Q_DISABLE_COPY(RtpChannel);

	friend class Private;
	friend class Producer;
	friend class ProducerPrivate;
	friend class Receiver;
	friend class ReceiverPrivate;
	friend class RtpChannelPrivate;
	RtpChannelPrivate *d;
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
	friend class ReceiverPrivate;
	Private *d;
};

// this class essentially follows jingle's notion of payload information,
//   though it's not really jingle-specific and should be usable for any RTP
//   purpose
class PayloadInfo
{
public:
	class Parameter
	{
	public:
		QString name;
		QString value;

		bool operator==(const Parameter &other) const;

		inline bool operator!=(const Parameter &other) const
		{
			return !(*this == other);
		}
	};

	PayloadInfo();
	PayloadInfo(const PayloadInfo &other);
	~PayloadInfo();
	PayloadInfo & operator=(const PayloadInfo &other);

	bool isNull() const;

	int id() const;
	QString name() const;
	int clockrate() const;
	int channels() const;
	int ptime() const;
	int maxptime() const;
	QList<Parameter> parameters() const;

	void setId(int i);
	void setName(const QString &str);
	void setClockrate(int i);
	void setChannels(int num);
	void setPtime(int i);
	void setMaxptime(int i);
	void setParameters(const QList<Parameter> &params);

	bool operator==(const PayloadInfo &other) const;

	inline bool operator!=(const PayloadInfo &other) const
	{
		return !(*this == other);
	}

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

	void setAudioOutputDevice(const QString &deviceId);
#ifdef QT_GUI_LIB
	void setVideoWidget(VideoWidget *widget);
#endif
	void setRecorder(Recorder *recorder);

	void setAudioPayloadInfo(const QList<PayloadInfo> &info);
	void setVideoPayloadInfo(const QList<PayloadInfo> &info);
	void setAudioParams(const QList<AudioParams> &params);
	void setVideoParams(const QList<VideoParams> &params);

	void start();
	void stop();

	QList<PayloadInfo> audioPayloadInfo() const;
	QList<PayloadInfo> videoPayloadInfo() const;
	QList<AudioParams> audioParams() const;
	QList<VideoParams> videoParams() const;

	int volume() const; // 0 (mute) to 100
	void setVolume(int level);

	Error errorCode() const;

	// offset 0 is write-only, offset 1 is read-write
	RtpChannel *audioRtpChannel();
	RtpChannel *videoRtpChannel();

signals:
	void started();
	void stopped();
	void error();

private:
	Q_DISABLE_COPY(Receiver);

	friend class ReceiverPrivate;
	ReceiverPrivate *d;
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

	void setAudioInputDevice(const QString &deviceId);
	void setVideoInputDevice(const QString &deviceId);
	void setFileInput(const QString &fileName);
	void setFileDataInput(const QByteArray &fileData);
#ifdef QT_GUI_LIB
	void setVideoWidget(VideoWidget *widget);
#endif

	void setAudioPayloadInfo(const QList<PayloadInfo> &info);
	void setVideoPayloadInfo(const QList<PayloadInfo> &info);
	void setAudioParams(const QList<AudioParams> &params);
	void setVideoParams(const QList<VideoParams> &params);

	void start();
	void transmitAudio(int paramsIndex = -1);
	void transmitVideo(int paramsIndex = -1);
	void pauseAudio();
	void pauseVideo();
	void stop();

	QList<PayloadInfo> audioPayloadInfo() const;
	QList<PayloadInfo> videoPayloadInfo() const;
	QList<AudioParams> audioParams() const;
	QList<VideoParams> videoParams() const;

	int volume() const; // 0 (mute) to 100
	void setVolume(int level);

	Error errorCode() const;

	// offset 0 is read-only, offset 1 is read-write
	RtpChannel *audioRtpChannel();
	RtpChannel *videoRtpChannel();

signals:
	void started();
	void stopped();
	void finished(); // for file playback only
	void error();

private:
	Q_DISABLE_COPY(Producer);

	friend class ProducerPrivate;
	ProducerPrivate *d;
};

}

#endif
