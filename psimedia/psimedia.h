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

class RtpSession;
class RtpSessionPrivate;
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

	virtual QSize sizeHint() const;

protected:
	virtual void paintEvent(QPaintEvent *event);

private:
	Q_DISABLE_COPY(VideoWidget);

	friend class VideoWidgetPrivate;
	friend class RtpSession;
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

class Features : public QObject
{
	Q_OBJECT

public:
	Features(QObject *parent = 0);
	~Features();

	void lookup();
	bool waitForFinished(int msecs = -1);

	QList<Device> audioOutputDevices();
	QList<Device> audioInputDevices();
	QList<Device> videoInputDevices();

	QList<AudioParams> supportedAudioModes();
	QList<VideoParams> supportedVideoModes();

signals:
	void finished();

private:
	class Private;
	friend class Private;
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
	friend class RtpSession;
	friend class RtpSessionPrivate;
	friend class RtpChannelPrivate;
	RtpChannelPrivate *d;
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

class RtpSession : public QObject
{
	Q_OBJECT

public:
	enum Error
	{
		ErrorGeneric,
		ErrorSystem,
		ErrorCodec
	};

	RtpSession(QObject *parent = 0);
	~RtpSession();

	void setAudioOutputDevice(const QString &deviceId);
#ifdef QT_GUI_LIB
	void setVideoOutputWidget(VideoWidget *widget);
#endif

	void setAudioInputDevice(const QString &deviceId);
	void setVideoInputDevice(const QString &deviceId);
	void setFileInput(const QString &fileName);
	void setFileDataInput(const QByteArray &fileData);
#ifdef QT_GUI_LIB
	void setVideoPreviewWidget(VideoWidget *widget);
#endif

	// pass a QIODevice to record to.  pass 0 to stop recording.  if a
	//   device is set before starting the session, then recording will
	//   wait until it starts.
	// records in ogg theora+vorbis format
	void setRecordingQIODevice(QIODevice *dev);

	// set local preferences.  this can be fuzzy *params structures
	//   or payloadinfo.  *params structures end up expanding to a
	//   bunch of payloadinfo structures.
	void setLocalAudioPreferences(const QList<AudioParams> &params);
	void setLocalAudioPreferences(const QList<PayloadInfo> &info);
	void setLocalVideoPreferences(const QList<VideoParams> &params);
	void setLocalVideoPreferences(const QList<PayloadInfo> &info);

	// set remote preferences.  this is always as payloadinfo.
	void setRemoteAudioPreferences(const QList<PayloadInfo> &info);
	void setRemoteVideoPreferences(const QList<PayloadInfo> &info);

	// usage strategy:
	//   - initiator sets local prefs as params
	//   - initiator starts(), waits for started()
	//   - initiator obtains the corresponding payloadinfos and sends to
	//     target.
	//   - target receives payloadinfos
	//   - target sets local prefs as params, and remote prefs
	//   - target starts(), waits for started()
	//   - target obtains the corresponding payloadinfos, which is an
	//     intersection of initiator/target preferences, and sends to
	//     initiator
	//   - target is ready for use
	//   - initiator receives payloadinfos, sets remote prefs, calls
	//     updatePreferences() and waits for preferencesUpdated()
	//   - initiator ready for use
	//
	// after starting, params getter functions will return a number
	//   of objects matching that of the payloadinfo getters.  note
	//   that these objects may not match the original local prefs
	//   params (if any).
	//
	// it is also possible to set local prefs as payloadinfo instead
	//   of params, but this is more for testing purposes than
	//   something you'd want to do in the real world.
	//
	// note: target must set at least two params (upper/lower bound)
	//   if it wants any flexibility in what payloadinfo is picked.
	//   for additional flexibility, fields in params may be left unset.
	//
	// adding audio/video to existing session lacking it:
	//   - set new local prefs as params
	//   - call updatePreferences(), wait for preferencesUpdated()
	//   - obtain corresponding payloadinfos, send to peer
	//   - peer receives payloadinfos, sets local prefs as params, and
	//     remote prefs
	//   - peer calls updatePreferences(), waits for preferencesUpdated()
	//   - peer obtains corresponding payloadinfos (intersection), and
	//     sends back
	//   - receive payloadinfos, set remote prefs, call
	//     updatePreferences() and wait for preferencesUpdated()
	//
	// modifying params of existing media types:
	//   - set new local prefs as params
	//   - if any prefs were added, then remote prefs are cleared
	//   - save original payloadinfos
	//   - call updatePreferences(), wait for preferencesUpdated()
	//   - obtain corresponding payloadinfos, and compare to original to
	//     determine what was added or removed
	//   - send adds/removes to peer
	//   - peer receives payloadinfos, sets remote prefs based on
	//     adds/removes to the original
	//   - peer calls updatePreferences(), waits for preferencesUpdated()
	//   - if there were any adds, peer obtains corresponding payloadinfos
	//     (intersection), and compares to original to determine what was
	//     agreed to be added.
	//   - peer acks back with accepted adds, or rejects
	//   - if reject is received, set original remote prefs
	//   - if accept is received, add the 'adds' to the original remote
	//     prefs and set them
	//   - call updatePreferences(), wait for preferencesUpdated()
	void start();

	// if prefs are changed after starting, this function needs to be
	//   called for them to take effect
	void updatePreferences();

	void transmitAudio(int index = -1);
	void transmitVideo(int index = -1);
	void pauseAudio();
	void pauseVideo();
	void stop();

	QList<PayloadInfo> audioPayloadInfo() const;
	QList<PayloadInfo> videoPayloadInfo() const;

	// maps to above payloadinfo.  may not necessarily match local prefs
	//   set as params.
	QList<AudioParams> audioParams() const;
	QList<VideoParams> videoParams() const;

	// speaker
	int outputVolume() const; // 0 (mute) to 100
	void setOutputVolume(int level);

	// microphone
	int inputVolume() const; // 0 (mute) to 100
	void setInputVolume(int level);

	Error errorCode() const;

	RtpChannel *audioRtpChannel();
	RtpChannel *videoRtpChannel();

signals:
	void started();
	void preferencesUpdated();
	void audioInputIntensityChanged(int intensity); // 0-100
	void stopped();
	void finished(); // for file playback only
	void error();

private:
	Q_DISABLE_COPY(RtpSession);

	friend class RtpSessionPrivate;
	RtpSessionPrivate *d;
};

}

#endif
