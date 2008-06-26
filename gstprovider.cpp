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

#include "psimediaprovider.h"

#include <QtCore>
#include <QImage>
#include <gst/gst.h>
#include "gstcustomelements.h"

namespace PsiMedia {

/*static QString hexEncode(const QByteArray &in)
{
	QString out;
	for(int n = 0; n < in.size(); ++n)
		out += QString().sprintf("%02x", (unsigned char)in[n]);
	return out;
}

static int hexValue(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	else if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	else if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else
		return -1;
}

static unsigned char hexByte(char hi, char lo)
{
	int value = (hexValue(hi) << 4) + hexValue(lo);
	return (unsigned char)value;
}

static QByteArray hexDecode(const QString &in)
{
	QByteArray out;
	for(int n = 0; n + 1 < in.length(); n += 2)
		out += hexByte(in[n].toLatin1(), in[n + 1].toLatin1());
	return out;
}*/

class GstProducerContext;

class GstRtpChannel : public QObject, public RtpChannelContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::RtpChannelContext)

public:
	friend class GstProducerContext;
	QList<PRtpPacket> in;

	virtual QObject *qobject()
	{
		return this;
	}

	virtual void setEnabled(bool b)
	{
		// TODO
		Q_UNUSED(b);
	}

	virtual int packetsAvailable() const
	{
		return in.count();
	}

	virtual PRtpPacket read()
	{
		return in.takeFirst();
	}

	virtual void write(const PRtpPacket &rtp)
	{
		// TODO
		Q_UNUSED(rtp);
	}

signals:
	void readyRead();
	void packetsWritten(int count);
};

class GstProducerContext : public QObject, public ProducerContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::ProducerContext)

public:
	QString audioInId, videoInId;
	QString fileIn;
	QByteArray fileDataIn;
	VideoWidgetContext *videoWidget;
	int audioInVolume;
	int code;

	GstRtpChannel audioRtp;
	GstRtpChannel videoRtp;

	GstProducerContext(QObject *parent = 0) :
		QObject(parent),
		videoWidget(0),
		audioInVolume(100),
		code(-1)
	{
	}

	virtual QObject *qobject()
	{
		return this;
	}

	virtual void setAudioInputDevice(const QString &deviceId)
	{
		audioInId = deviceId;
		// TODO: if active, switch to that device
	}

	virtual void setVideoInputDevice(const QString &deviceId)
	{
		videoInId = deviceId;
		// TODO: if active, switch to that device
	}

	virtual void setFileInput(const QString &fileName)
	{
		fileIn = fileName;
		// TODO: if active, switch to playing this file
	}

	virtual void setFileDataInput(const QByteArray &fileData)
	{
		fileDataIn = fileData;
		// TODO: if active, switch to playing this file data
	}

#ifdef QT_GUI_LIB
	virtual void setVideoWidget(VideoWidgetContext *widget)
	{
		videoWidget = widget;
		// TODO: if active, switch to using (or not using)
	}
#endif

	virtual void setAudioPayloadInfo(const QList<PPayloadInfo> &info)
	{
		// TODO
		Q_UNUSED(info);
	}

	virtual void setVideoPayloadInfo(const QList<PPayloadInfo> &info)
	{
		// TODO
		Q_UNUSED(info);
	}

	virtual void setAudioParams(const QList<PAudioParams> &params)
	{
		// TODO
		Q_UNUSED(params);
	}

	virtual void setVideoParams(const QList<PVideoParams> &params)
	{
		// TODO
		Q_UNUSED(params);
	}

	virtual void start()
	{
		// TODO: for now we have no send support, so just throw error
		QMetaObject::invokeMethod(this, "error", Qt::QueuedConnection);
	}

	virtual void transmitAudio(int paramsIndex)
	{
		// TODO (note that -1 means pick best)
		Q_UNUSED(paramsIndex);
	}

	virtual void transmitVideo(int paramsIndex)
	{
		// TODO (note that -1 means pick best)
		Q_UNUSED(paramsIndex);
	}

	virtual void pauseAudio()
	{
		// TODO
	}

	virtual void pauseVideo()
	{
		// TODO
	}

	virtual void stop()
	{
		// TODO
	}

	virtual QList<PPayloadInfo> audioPayloadInfo() const
	{
		// TODO
		return QList<PPayloadInfo>();
	}

	virtual QList<PPayloadInfo> videoPayloadInfo() const
	{
		// TODO
		return QList<PPayloadInfo>();
	}

	virtual QList<PAudioParams> audioParams() const
	{
		// TODO
		return QList<PAudioParams>();
	}

	virtual QList<PVideoParams> videoParams() const
	{
		// TODO
		return QList<PVideoParams>();
	}

	virtual int volume() const
	{
		return audioInVolume;
	}

	virtual void setVolume(int level)
	{
		audioInVolume = level;
		// TODO: if active, change active volume
	}

	virtual Error errorCode() const
	{
		return (Error)code;
	}

	virtual RtpChannelContext *audioRtpChannel()
	{
		return &audioRtp;
	}

	virtual RtpChannelContext *videoRtpChannel()
	{
		return &videoRtp;
	}

signals:
	void started();
	void stopped();
	void finished();
	void error();
};

class GstReceiverContext : public QObject, public ReceiverContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::ReceiverContext)

public:
	GstReceiverContext(QObject *parent = 0) :
		QObject(parent)
	{
	}

	virtual QObject *qobject()
	{
		return this;
	}

	virtual void setAudioOutputDevice(const QString &deviceId)
	{
		// TODO
		Q_UNUSED(deviceId);
	}

#ifdef QT_GUI_LIB
	virtual void setVideoWidget(VideoWidgetContext *widget)
	{
		// TODO
		Q_UNUSED(widget);
	}
#endif
	virtual void setRecorder(QIODevice *recordDevice)
	{
		// TODO
		Q_UNUSED(recordDevice);
	}

	virtual void setAudioPayloadInfo(const QList<PPayloadInfo> &info)
	{
		// TODO
		Q_UNUSED(info);
	}

	virtual void setVideoPayloadInfo(const QList<PPayloadInfo> &info)
	{
		// TODO
		Q_UNUSED(info);
	}

	virtual void setAudioParams(const QList<PAudioParams> &params)
	{
		// TODO
		Q_UNUSED(params);
	}

	virtual void setVideoParams(const QList<PVideoParams> &params)
	{
		// TODO
		Q_UNUSED(params);
	}

	virtual void start()
	{
		// TODO: for now we have no receive support, so just throw error
		QMetaObject::invokeMethod(this, "error", Qt::QueuedConnection);
	}

	virtual void stop()
	{
		// TODO
	}

	virtual QList<PPayloadInfo> audioPayloadInfo() const
	{
		// TODO
		return QList<PPayloadInfo>();
	}

	virtual QList<PPayloadInfo> videoPayloadInfo() const
	{
		// TODO
		return QList<PPayloadInfo>();
	}

	virtual QList<PAudioParams> audioParams() const
	{
		// TODO
		return QList<PAudioParams>();
	}

	virtual QList<PVideoParams> videoParams() const
	{
		// TODO
		return QList<PVideoParams>();
	}

	virtual int volume() const
	{
		// TODO
		return 0;
	}

	virtual void setVolume(int level)
	{
		// TODO
		Q_UNUSED(level);
	}

	virtual Error errorCode() const
	{
		// TODO
		return ErrorGeneric;
	}

	virtual RtpChannelContext *audioRtpChannel()
	{
		// TODO
		return 0;
	}

	virtual RtpChannelContext *videoRtpChannel()
	{
		// TODO
		return 0;
	}

signals:
	void started();
	void stopped();
	void error();
};

class GstProvider : public QObject, public Provider
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::Provider)

public:
	virtual QObject *qobject()
	{
		return this;
	}

	virtual bool init(const QString &resourcePath)
	{
		// TODO
		Q_UNUSED(resourcePath);
		return true;
	}

	virtual QString creditName()
	{
		return "GStreamer";
	}

	virtual QString creditText()
	{
		QString str =
		"This application uses GStreamer, a comprehensive "
		"open-source and cross-platform multimedia framework.  For "
		"more information, see http://www.gstreamer.net/\n\n"
		"If you enjoy this software, please give the GStreamer "
		"people a million dollars.";
		return str;
	}

	// FIXME: bunch of hardcoding below

	virtual QList<PAudioParams> supportedAudioModes()
	{
		QList<PAudioParams> list;
		{
			PAudioParams p;
			p.codec = "speex";
			p.sampleRate = 8000;
			p.sampleSize = 16;
			p.channels = 1;
			list += p;
		}
		{
			PAudioParams p;
			p.codec = "speex";
			p.sampleRate = 16000;
			p.sampleSize = 16;
			p.channels = 1;
			list += p;
		}
		{
			PAudioParams p;
			p.codec = "speex";
			p.sampleRate = 32000;
			p.sampleSize = 16;
			p.channels = 1;
			list += p;
		}
		return list;
	}

	virtual QList<PVideoParams> supportedVideoModes()
	{
		QList<PVideoParams> list;
		{
			PVideoParams p;
			p.codec = "theora";
			p.size = QSize(160, 120);
			p.fps = 15;
			list += p;
		}
		{
			PVideoParams p;
			p.codec = "theora";
			p.size = QSize(320, 240);
			p.fps = 15;
			list += p;
		}
		{
			PVideoParams p;
			p.codec = "theora";
			p.size = QSize(320, 240);
			p.fps = 30;
			list += p;
		}
		return list;
	}

	// some fake devices for now
	virtual QList<PDevice> audioOutputDevices()
	{
		QList<PDevice> list;
		{
			PDevice dev;
			dev.type = PDevice::AudioOut;
			dev.id = "default";
			dev.name = "STAC92xx Analog";
			list += dev;
		}
		return list;
	}

	virtual QList<PDevice> audioInputDevices()
	{
		QList<PDevice> list;
		{
			PDevice dev;
			dev.type = PDevice::AudioIn;
			dev.id = "default";
			dev.name = "USB Audio";
			list += dev;
		}
		return list;
	}

	virtual QList<PDevice> videoInputDevices()
	{
		QList<PDevice> list;
		{
			PDevice dev;
			dev.type = PDevice::VideoIn;
			dev.id = "default";
			dev.name = "Laptop Integrated Webcam";
			list += dev;
		}
		return list;
	}

	virtual ProducerContext *createProducer()
	{
		return new GstProducerContext;
	}

	virtual ReceiverContext *createReceiver()
	{
		return new GstReceiverContext;
	}
};

class GstPlugin : public QObject, public Plugin
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::Plugin)

public:
	virtual Provider *createProvider() { return new GstProvider; }
};

}

Q_EXPORT_PLUGIN2(gstprovider, PsiMedia::GstPlugin)

#include "gstprovider.moc"
