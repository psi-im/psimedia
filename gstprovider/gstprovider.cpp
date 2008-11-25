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

#include <QStringList>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QtPlugin>
#include "devices.h"
#include "modes.h"
#include "gstthread.h"
#include "rwcontrol.h"

namespace PsiMedia {

static PDevice gstDeviceToPDevice(const GstDevice &dev, PDevice::Type type)
{
	PDevice out;
	out.type = type;
	out.name = dev.name;
	out.id = dev.id;
	return out;
}

//----------------------------------------------------------------------------
// GstRtpChannel
//----------------------------------------------------------------------------
// for a live transmission we really shouldn't have excessive queuing (or
//   *any* queuing!), so we'll cap the queue sizes.  if the system gets
//   overloaded and the thread scheduling skews such that our queues get
//   filled before they can be emptied, then we'll start dropping old
//   items making room for new ones.  on a live transmission there's no
//   sense in keeping ancient data around.  we just drop and move on.
#define QUEUE_PACKET_MAX 25

// don't wake the main thread more often than this, for performance reasons
#define WAKE_PACKET_MIN 40

class GstRtpSessionContext;

class GstRtpChannel : public QObject, public RtpChannelContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::RtpChannelContext)

public:
	bool enabled;
	QMutex m;
	GstRtpSessionContext *session;
	QList<PRtpPacket> in;

	QTime wake_time;
	bool wake_pending;
	QList<PRtpPacket> pending_in;

	int written_pending;

	GstRtpChannel() :
		QObject(),
		enabled(false),
		wake_pending(false),
		written_pending(0)
	{
	}

	virtual QObject *qobject()
	{
		return this;
	}

	virtual void setEnabled(bool b)
	{
		QMutexLocker locker(&m);
		enabled = b;
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
		m.lock();
		if(!enabled)
			return;
		m.unlock();

		receiver_push_packet_for_write(rtp);
		++written_pending;

		// only queue one call per eventloop pass
		if(written_pending == 1)
			QMetaObject::invokeMethod(this, "processOut", Qt::QueuedConnection);
	}

	// session calls this, which may be in another thread
	void push_packet_for_read(const PRtpPacket &rtp)
	{
		QMutexLocker locker(&m);
		if(!enabled)
			return;

		// if the queue is full, bump off the oldest to make room
		if(pending_in.count() >= QUEUE_PACKET_MAX)
			pending_in.removeFirst();

		pending_in += rtp;

		// TODO: use WAKE_PACKET_MIN and wake_time ?

		if(!wake_pending)
		{
			wake_pending = true;
			QMetaObject::invokeMethod(this, "processIn", Qt::QueuedConnection);
		}
	}

signals:
	void readyRead();
	void packetsWritten(int count);

private slots:
	void processIn()
	{
		int oldcount = in.count();

		m.lock();
		wake_pending = false;
		in += pending_in;
		pending_in.clear();
		m.unlock();

		if(in.count() > oldcount)
			emit readyRead();
	}

	void processOut()
	{
		int count = written_pending;
		written_pending = 0;
		emit packetsWritten(count);
	}

private:
	void receiver_push_packet_for_write(const PRtpPacket &rtp);
};

//----------------------------------------------------------------------------
// GstRtpSessionContext
//----------------------------------------------------------------------------
class GstRtpSessionContext : public QObject, public RtpSessionContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::RtpSessionContext)

public:
	GstThread *gstThread;

	RwControlLocal *control;
	RwControlConfigDevices devices;
	RwControlConfigCodecs codecs;
	RwControlTransmit transmit;
	RwControlStatus lastStatus;
	bool isStarted;
	bool pending_status;

#ifdef QT_GUI_LIB
	VideoWidgetContext *outputWidget, *previewWidget;
#endif

	QIODevice *recordDevice, *nextRecordDevice;
	bool record_cancel;

	// keep these parentless, so they can switch threads
	GstRtpChannel audioRtp;
	GstRtpChannel videoRtp;

	GstRtpSessionContext(GstThread *_gstThread, QObject *parent = 0) :
		QObject(parent),
		gstThread(_gstThread),
		control(0),
		pending_status(false),
		recordDevice(0),
		nextRecordDevice(0),
		record_cancel(false)
	{
#ifdef QT_GUI_LIB
		outputWidget = 0;
		previewWidget = 0;
#endif

		devices.audioOutVolume = 100;
		devices.audioInVolume = 100;

		codecs.useLocalAudioParams = true;
		codecs.useLocalVideoParams = true;
	}

	~GstRtpSessionContext()
	{
		cleanup();
	}

	virtual QObject *qobject()
	{
		return this;
	}

	void cleanup()
	{
		isStarted = false;
		pending_status = false;
		delete control;
		control = 0;
	}

	virtual void setAudioOutputDevice(const QString &deviceId)
	{
		devices.audioOutId = deviceId;
		if(control)
			control->updateDevices(devices);
	}

	virtual void setAudioInputDevice(const QString &deviceId)
	{
		devices.audioInId = deviceId;
		if(control)
			control->updateDevices(devices);
	}

	virtual void setVideoInputDevice(const QString &deviceId)
	{
		devices.videoInId = deviceId;
		if(control)
			control->updateDevices(devices);
	}

	virtual void setFileInput(const QString &fileName)
	{
		devices.fileNameIn = fileName;
		devices.fileDataIn.clear();
		if(control)
			control->updateDevices(devices);
	}

	virtual void setFileDataInput(const QByteArray &fileData)
	{
		devices.fileDataIn = fileData;
		devices.fileNameIn.clear();
		if(control)
			control->updateDevices(devices);
	}

	virtual void setFileLoopEnabled(bool enabled)
	{
		devices.loopFile = enabled;
		if(control)
			control->updateDevices(devices);
	}

#ifdef QT_GUI_LIB
        virtual void setVideoOutputWidget(VideoWidgetContext *widget)
	{
		outputWidget = widget;
		devices.useVideoOut = widget ? true : false;
		if(control)
			control->updateDevices(devices);
	}

	virtual void setVideoPreviewWidget(VideoWidgetContext *widget)
	{
		previewWidget = widget;
		devices.useVideoPreview = widget ? true : false;
		if(control)
			control->updateDevices(devices);
	}
#endif

	virtual void setRecorder(QIODevice *_recordDevice)
	{
		if(recordDevice)
		{
			// if we were already recording and haven't cancelled,
			//   then cancel it
			if(!record_cancel)
			{
				record_cancel = true;

				RwControlRecord record;
				record.enabled = false;
				control->setRecord(record);
			}

			// queue up the device for later
			if(_recordDevice)
				nextRecordDevice = _recordDevice;
		}
		else
		{
			if(control)
			{
				if(_recordDevice)
				{
					recordDevice = _recordDevice;

					RwControlRecord record;
					record.enabled = true;
					control->setRecord(record);
				}
			}
			else
			{
				// queue up the device for later
				nextRecordDevice = _recordDevice;
			}
		}
	}

	virtual void setLocalAudioPreferences(const QList<PAudioParams> &params)
	{
		codecs.useLocalAudioParams = true;
		codecs.localAudioParams = params;

		// disable the other
		codecs.useLocalAudioPayloadInfo = false;
		codecs.localAudioPayloadInfo.clear();
	}

	virtual void setLocalAudioPreferences(const QList<PPayloadInfo> &info)
	{
		codecs.useLocalAudioPayloadInfo = true;
		codecs.localAudioPayloadInfo = info;

		// disable the other
		codecs.useLocalAudioParams = false;
		codecs.localAudioParams.clear();
	}

	virtual void setLocalVideoPreferences(const QList<PVideoParams> &params)
	{
		codecs.useLocalVideoParams = true;
		codecs.localVideoParams = params;

		// disable the other
		codecs.useLocalVideoPayloadInfo = false;
		codecs.localVideoPayloadInfo.clear();
	}

	virtual void setLocalVideoPreferences(const QList<PPayloadInfo> &info)
	{
		codecs.useLocalVideoPayloadInfo = true;
		codecs.localVideoPayloadInfo = info;

		// disable the other
		codecs.useLocalVideoParams = false;
		codecs.localVideoParams.clear();
	}

	virtual void setRemoteAudioPreferences(const QList<PPayloadInfo> &info)
	{
		codecs.useRemoteAudioPayloadInfo = true;
		codecs.remoteAudioPayloadInfo = info;
	}

	virtual void setRemoteVideoPreferences(const QList<PPayloadInfo> &info)
	{
		codecs.useRemoteVideoPayloadInfo = true;
		codecs.remoteVideoPayloadInfo = info;
	}

	virtual void start()
	{
		Q_ASSERT(!control && !isStarted);

		control = new RwControlLocal(gstThread, this);
		connect(control, SIGNAL(statusReady(const RwControlStatus &)), SLOT(control_statusReady(const RwControlStatus &)));
		connect(control, SIGNAL(previewFrame(const QImage &)), SLOT(control_previewFrame(const QImage &)));
		connect(control, SIGNAL(outputFrame(const QImage &)), SLOT(control_outputFrame(const QImage &)));
		connect(control, SIGNAL(audioIntensityChanged(int)), SLOT(control_audioIntensityChanged(int)));

		lastStatus = RwControlStatus();
		isStarted = false;
		pending_status = true;
		control->start(devices, codecs);
	}

	virtual void updatePreferences()
	{
		Q_ASSERT(control);

		pending_status = true;
		control->updateCodecs(codecs);
	}

	virtual void transmitAudio(int index)
	{
		transmit.useAudio = true;
		transmit.audioIndex = index;
		control->setTransmit(transmit);
	}

	virtual void transmitVideo(int index)
	{
		transmit.useVideo = true;
		transmit.videoIndex = index;
		control->setTransmit(transmit);
	}

	virtual void pauseAudio()
	{
		transmit.useAudio = false;
		control->setTransmit(transmit);
	}

	virtual void pauseVideo()
	{
		transmit.useVideo = false;
		control->setTransmit(transmit);
	}

	virtual void stop()
	{
		control->stop();
	}

	virtual QList<PPayloadInfo> audioPayloadInfo() const
	{
		return lastStatus.localAudioPayloadInfo;
	}

	virtual QList<PPayloadInfo> videoPayloadInfo() const
	{
		return lastStatus.localVideoPayloadInfo;
	}

	virtual QList<PAudioParams> audioParams() const
	{
		return lastStatus.localAudioParams;
	}

	virtual QList<PVideoParams> videoParams() const
	{
		return lastStatus.localVideoParams;
	}

	virtual bool canTransmitAudio() const
	{
		return lastStatus.canTransmitAudio;
	}

	virtual bool canTransmitVideo() const
	{
		return lastStatus.canTransmitVideo;
	}

	virtual int outputVolume() const
	{
		return devices.audioOutVolume;
	}

	virtual void setOutputVolume(int level)
	{
		devices.audioOutVolume = level;
		if(control)
			control->updateDevices(devices);
	}

	virtual int inputVolume() const
	{
		return devices.audioInVolume;
	}

	virtual void setInputVolume(int level)
	{
		devices.audioInVolume = level;
		if(control)
			control->updateDevices(devices);
	}

	virtual Error errorCode() const
	{
		return (Error)lastStatus.errorCode;
	}

	virtual RtpChannelContext *audioRtpChannel()
	{
		return &audioRtp;
	}

	virtual RtpChannelContext *videoRtpChannel()
	{
		return &videoRtp;
	}

	// channel calls this, which may be in another thread
	void push_packet_for_write(GstRtpChannel *from, const PRtpPacket &rtp)
	{
		// TODO: thread-safe access to 'control' which might get deleted?
		if(from == &audioRtp)
			control->rtpAudioIn(rtp);
		else if(from == &videoRtp)
			control->rtpVideoIn(rtp);
	}

signals:
	void started();
	void preferencesUpdated();
	void audioInputIntensityChanged(int intensity);
	void stopped();
	void finished();
	void error();

private slots:
	void control_statusReady(const RwControlStatus &status)
	{
		lastStatus = status;

		if(status.finished)
		{
			// finished status just means the file is done
			//   sending.  the session still remains active.
			emit finished();
		}
		else if(status.error)
		{
			cleanup();
			emit error();
		}
		else if(pending_status)
		{
			pending_status = false;

			if(status.stopped)
			{
				cleanup();
				emit stopped();
			}
			else if(!isStarted)
			{
				isStarted = true;

				if(!recordDevice && nextRecordDevice)
				{
					recordDevice = nextRecordDevice;
					nextRecordDevice = 0;

					RwControlRecord record;
					record.enabled = true;
					control->setRecord(record);
				}

				emit started();
			}
			else
				emit preferencesUpdated();
		}
	}

	void control_previewFrame(const QImage &img)
	{
		if(previewWidget)
			previewWidget->show_frame(img);
	}

	void control_outputFrame(const QImage &img)
	{
		if(outputWidget)
			outputWidget->show_frame(img);
	}

	void control_audioIntensityChanged(int intensity)
	{
		emit audioInputIntensityChanged(intensity);
	}

private:
	static void cb_control_rtpAudioOut(const PRtpPacket &packet, void *app)
	{
		((GstRtpSessionContext *)app)->control_rtpAudioOut(packet);
	}

	static void cb_control_rtpVideoOut(const PRtpPacket &packet, void *app)
	{
		((GstRtpSessionContext *)app)->control_rtpVideoOut(packet);
	}

	static void cb_control_recordData(const QByteArray &packet, void *app)
	{
		((GstRtpSessionContext *)app)->control_recordData(packet);
	}

	// note: this is executed from a different thread
	void control_rtpAudioOut(const PRtpPacket &packet)
	{
		audioRtp.push_packet_for_read(packet);
	}

	// note: this is executed from a different thread
	void control_rtpVideoOut(const PRtpPacket &packet)
	{
		videoRtp.push_packet_for_read(packet);
	}

	// note: this is executed from a different thread
	void control_recordData(const QByteArray &packet)
	{
		Q_UNUSED(packet);
		// TODO: if EOF (empty packet), be sure to start the
		//   next recording afterwards, like this:

		/*recordDevice = 0;
		record_cancel = false;

		if(nextRecordDevice)
		{
			recordDevice = nextRecordDevice;
			nextRecordDevice = 0;

			RwControlRecord record;
			record.enabled = true;
			control->setRecord(record);
		}*/
	}
};

void GstRtpChannel::receiver_push_packet_for_write(const PRtpPacket &rtp)
{
	if(session)
		session->push_packet_for_write(this, rtp);
}

//----------------------------------------------------------------------------
// GstProvider
//----------------------------------------------------------------------------
class GstProvider : public QObject, public Provider
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::Provider)

public:
	GstThread *thread;

	GstProvider() :
		thread(0)
	{
	}

	virtual QObject *qobject()
	{
		return this;
	}

	virtual bool init(const QString &resourcePath)
	{
		thread = new GstThread(this);
		if(!thread->start(resourcePath))
		{
			delete thread;
			thread = 0;
			return false;
		}

		return true;
	}

	~GstProvider()
	{
		delete thread;
	}

	virtual QString creditName()
	{
		return "GStreamer";
	}

	virtual QString creditText()
	{
		QString str = QString(
		"This application uses GStreamer %1, a comprehensive "
		"open-source and cross-platform multimedia framework.  For "
		"more information, see http://www.gstreamer.net/\n\n"
		"If you enjoy this software, please give the GStreamer "
		"people a million dollars."
		).arg(thread->gstVersion());
		return str;
	}

	virtual QList<PAudioParams> supportedAudioModes()
	{
		return modes_supportedAudio();
	}

	virtual QList<PVideoParams> supportedVideoModes()
	{
		return modes_supportedVideo();
	}

	virtual QList<PDevice> audioOutputDevices()
	{
		QList<PDevice> list;
		foreach(const GstDevice &i, devices_list(PDevice::AudioOut))
			list += gstDeviceToPDevice(i, PDevice::AudioOut);
		return list;
	}

	virtual QList<PDevice> audioInputDevices()
	{
		QList<PDevice> list;
		foreach(const GstDevice &i, devices_list(PDevice::AudioIn))
			list += gstDeviceToPDevice(i, PDevice::AudioIn);
		return list;
	}

	virtual QList<PDevice> videoInputDevices()
	{
		QList<PDevice> list;
		foreach(const GstDevice &i, devices_list(PDevice::VideoIn))
			list += gstDeviceToPDevice(i, PDevice::VideoIn);
		return list;
	}

	virtual RtpSessionContext *createRtpSession()
	{
		return new GstRtpSessionContext(thread);
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
