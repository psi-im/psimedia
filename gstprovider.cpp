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

namespace PsiMedia {

class GstProducerContext : public QObject, public ProducerContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::ProducerContext)

public:
	GstProducerContext(QObject *parent = 0) :
		QObject(parent)
	{
	}

	virtual void setAudioInputDevice(const QString &deviceId)
	{
	}

	virtual void setVideoInputDevice(const QString &deviceId)
	{
	}

	virtual void setFileInput(const QString &fileName)
	{
	}

	virtual void setFileDataInput(const QByteArray &fileData)
	{
	}

#ifdef QT_GUI_LIB
	virtual void setVideoWidget(VideoWidgetContext *widget)
	{
	}
#endif

	virtual void setAudioPayloadInfo(const QList<PPayloadInfo> &info)
	{
	}

	virtual void setVideoPayloadInfo(const QList<PPayloadInfo> &info)
	{
	}

	virtual void setAudioParams(const QList<PAudioParams> &params)
	{
	}

	virtual void setVideoParams(const QList<PVideoParams> &params)
	{
	}

	virtual void start()
	{
	}

	virtual void transmitAudio(int paramsIndex)
	{
	}

	virtual void transmitVideo(int paramsIndex)
	{
	}

	virtual void pauseAudio()
	{
	}

	virtual void pauseVideo()
	{
	}

	virtual void stop()
	{
	}

	virtual QList<PPayloadInfo> audioPayloadInfo() const
	{
	}

	virtual QList<PPayloadInfo> videoPayloadInfo() const
	{
	}

	virtual QList<PAudioParams> audioParams() const
	{
	}

	virtual QList<PVideoParams> videoParams() const
	{
	}

	virtual int volume() const
	{
	}

	virtual void setVolume(int level)
	{
	}

	virtual Error errorCode() const
	{
	}

	virtual RtpChannelContext *audioRtpChannel()
	{
	}

	virtual RtpChannelContext *videoRtpChannel()
	{
	}

signals:
	void started();
	void stopped();
	void finished();
	void error();
};

class GstProvider : public QObject, public Provider
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::Provider)

public:
	virtual bool init(const QString &resourcePath)
	{
		Q_UNUSED(resourcePath);
		return true;
	}

	virtual void initEngine()
	{
		QMetaObject::invokeMethod(this, "initEngineFinished", Qt::QueuedConnection);
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

	virtual ProducerContext *createProducer()
	{
		return new GstProducerContext;
	}

	virtual ReceiverContext *createReceiver()
	{
		return 0;
	}

signals:
	void initEngineFinished();
};

}

Q_EXPORT_PLUGIN2(gstprovider, PsiMedia::GstProvider)

#include "gstprovider.moc"
