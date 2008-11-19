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
#include <QDir>
#include <QMutex>
#include <QWaitCondition>
#include <QCoreApplication>
#include <QLibrary>
#include <QThread>
#include <QtPlugin>
#include <gst/gst.h>
#include "gstcustomelements/gstcustomelements.h"
#include "devices.h"
#include "payloadinfo.h"
#include "rtpworker.h"

namespace PsiMedia {

static QList<GstDevice> gstAudioOutputDevices()
{
	return devices_list(PDevice::AudioOut);
}

static QList<GstDevice> gstAudioInputDevices()
{
	return devices_list(PDevice::AudioIn);
}

static QList<GstDevice> gstVideoInputDevices()
{
	return devices_list(PDevice::VideoIn);
}

static PDevice gstDeviceToPDevice(const GstDevice &dev, PDevice::Type type)
{
	PDevice out;
	out.type = type;
	out.name = dev.name;
	out.id = dev.id;
	return out;
}

//----------------------------------------------------------------------------
// GstSession
//----------------------------------------------------------------------------
// converts Qt-ified commandline args back to C style
class CArgs
{
public:
	int argc;
	char **argv;

	CArgs()
	{
		argc = 0;
		argv = 0;
	}

	~CArgs()
	{
		if(count > 0)
		{
			for(int n = 0; n < count; ++n)
				free(data[n]);
			free(argv);
			free(data);
		}
	}

	void set(const QStringList &args)
	{
		count = args.count();
		if(count == 0)
		{
			data = 0;
			argc = 0;
			argv = 0;
		}
		else
		{
			data = (char **)malloc(sizeof(char **) * count);
			argv = (char **)malloc(sizeof(char **) * count);
			for(int n = 0; n < count; ++n)
			{
				QByteArray cs = args[n].toLocal8Bit();
				data[n] = (char *)qstrdup(cs.data());
				argv[n] = data[n];
			}
			argc = count;
		}
	}

private:
	int count;
	char **data;
};

static void loadPlugins(const QString &pluginPath, bool print = false)
{
	if(print)
		printf("Loading plugins in [%s]\n", qPrintable(pluginPath));
	QDir dir(pluginPath);
	QStringList entryList = dir.entryList(QDir::Files);
	foreach(QString entry, entryList)
	{
		if(!QLibrary::isLibrary(entry))
			continue;
		QString filePath = dir.filePath(entry);
		GError *err = 0;
		GstPlugin *plugin = gst_plugin_load_file(
			filePath.toLatin1().data(), &err);
		if(!plugin)
		{
			if(print)
			{
				printf("**FAIL**: %s: %s\n", qPrintable(entry),
					err->message);
			}
			g_error_free(err);
			continue;
		}
		if(print)
		{
			printf("   OK   : %s name=[%s]\n", qPrintable(entry),
				gst_plugin_get_name(plugin));
		}
		gst_object_unref(plugin);
	}

	if(print)
		printf("\n");
}

class GstSession
{
public:
	CArgs args;
	QString version;

	GstSession(const QString &pluginPath = QString())
	{
		args.set(QCoreApplication::instance()->arguments());

		// make sure glib threads are available
		if(!g_thread_supported())
			g_thread_init(NULL);

		// you can also use NULLs here if you don't want to pass args
		GError *error;
		if(!gst_init_check(&args.argc, &args.argv, &error))
		{
			// TODO: report fail
		}

		guint major, minor, micro, nano;
		gst_version(&major, &minor, &micro, &nano);

		QString nano_str;
		if(nano == 1)
			nano_str = " (CVS)";
		else if(nano == 2)
			nano_str = " (Prerelease)";

		version.sprintf("%d.%d.%d%s", major, minor, micro,
			!nano_str.isEmpty() ? qPrintable(nano_str) : "");

		// manually load plugins?
		if(!pluginPath.isEmpty())
			loadPlugins(pluginPath);

		gstcustomelements_register();
	}

	~GstSession()
	{
		// nothing i guess
	}
};

//----------------------------------------------------------------------------
// GstThread
//----------------------------------------------------------------------------
Q_GLOBAL_STATIC(QMutex, render_mutex)
class GstRtpSessionContext;
static GstRtpSessionContext *g_producer = 0;
static GstRtpSessionContext *g_receiver = 0;
static QList<QImage> *g_images = 0;

class GstRtpChannel;
static void receiver_write(GstRtpChannel *from, const PRtpPacket &rtp);
static QList<QImage> *g_rimages = 0;

Q_GLOBAL_STATIC(QMutex, in_mutex)
static QList<PRtpPacket> *g_in_packets_audio = 0;
static QList<PRtpPacket> *g_in_packets = 0;

class GstThread : public QThread
{
	Q_OBJECT

public:
	QString pluginPath;
	GstSession *gstSession;
	GMainContext *mainContext;
	GMainLoop *mainLoop;
	QMutex m;
	QWaitCondition w;
	static GstThread *self;

	RtpWorker *worker;

	bool producerMode;

	GstThread(QObject *parent = 0) :
		QThread(parent),
		gstSession(0),
		mainContext(0),
		mainLoop(0),
		worker(0)
	{
		self = this;
	}

	~GstThread()
	{
		stop();
		self = 0;
	}

	static GstThread *instance()
	{
		return self;
	}

	void start(const QString &_pluginPath = QString())
	{
		QMutexLocker locker(&m);
		pluginPath = _pluginPath;
		QThread::start();
		w.wait(&m);
	}

	void stop()
	{
		QMutexLocker locker(&m);
		if(mainLoop)
		{
			// thread-safe ?
			g_main_loop_quit(mainLoop);
			w.wait(&m);
		}

		wait();
	}

	void startProducer()
	{
		GSource *timer = g_timeout_source_new(0);
		g_source_set_callback(timer, cb_doStartProducer, this, NULL);
		g_source_attach(timer, mainContext);
	}

	void startReceiver()
	{
		GSource *timer = g_timeout_source_new(0);
		g_source_set_callback(timer, cb_doStartReceiver, this, NULL);
		g_source_attach(timer, mainContext);
	}

	void stopProducer()
	{
		GSource *timer = g_timeout_source_new(0);
		g_source_set_callback(timer, cb_doStopProducer, this, NULL);
		g_source_attach(timer, mainContext);
	}

	void stopReceiver()
	{
		GSource *timer = g_timeout_source_new(0);
		g_source_set_callback(timer, cb_doStopReceiver, this, NULL);
		g_source_attach(timer, mainContext);
	}

signals:
	void producer_started();
	void producer_stopped();
	void producer_finished();
	void producer_error();

	void receiver_started();
	void receiver_stopped();

protected:
	virtual void run()
	{
		printf("GStreamer thread started\n");

		 // this will be unlocked as soon as the mainloop runs
		m.lock();

		gstSession = new GstSession(pluginPath);
		printf("Using GStreamer version %s\n", qPrintable(gstSession->version));

		mainContext = g_main_context_new();
		mainLoop = g_main_loop_new(mainContext, FALSE);

		// deferred call to loop_started()
		GSource *timer = g_timeout_source_new(0);
		g_source_attach(timer, mainContext);
		g_source_set_callback(timer, cb_loop_started, this, NULL);

		// kick off the event loop
		g_main_loop_run(mainLoop);

		QMutexLocker locker(&m);

		// cleanup
		delete worker;
		worker = 0;

		g_main_loop_unref(mainLoop);
		mainLoop = 0;
		g_main_context_unref(mainContext);
		mainContext = 0;
		delete gstSession;
		gstSession = 0;

		w.wakeOne();
		printf("GStreamer thread completed\n");
	}

private:
	static gboolean cb_loop_started(gpointer data)
	{
		return ((GstThread *)data)->loop_started();
	}

	static gboolean cb_doStartProducer(gpointer data)
	{
		return ((GstThread *)data)->doStartProducer();
	}

	static gboolean cb_doStartReceiver(gpointer data)
	{
		return ((GstThread *)data)->doStartReceiver();
	}

	static gboolean cb_doStopProducer(gpointer data)
	{
		return ((GstThread *)data)->doStopProducer();
	}

	static gboolean cb_doStopReceiver(gpointer data)
	{
		return ((GstThread *)data)->doStopReceiver();
	}

	static void cb_worker_started(void *app)
	{
		((GstThread *)app)->worker_started();
	}

	static void cb_worker_updated(void *app)
	{
		((GstThread *)app)->worker_updated();
	}

	static void cb_worker_stopped(void *app)
	{
		((GstThread *)app)->worker_stopped();
	}

	static void cb_worker_finished(void *app)
	{
		((GstThread *)app)->worker_finished();
	}

	static void cb_worker_error(void *app)
	{
		((GstThread *)app)->worker_error();
	}

	static void cb_worker_previewFrame(const QImage &img, void *app)
	{
		((GstThread *)app)->worker_previewFrame(img);
	}

	static void cb_worker_outputFrame(const QImage &img, void *app)
	{
		((GstThread *)app)->worker_outputFrame(img);
	}

	static void cb_worker_rtpAudioOut(const PRtpPacket &packet, void *app)
	{
		((GstThread *)app)->worker_rtpAudioOut(packet);
	}

	static void cb_worker_rtpVideoOut(const PRtpPacket &packet, void *app)
	{
		((GstThread *)app)->worker_rtpVideoOut(packet);
	}

	gboolean loop_started()
	{
		worker = new RtpWorker(mainContext);

		w.wakeOne();
		m.unlock();
		return FALSE;
	}

	gboolean doStartProducer()
	{
		worker->start();
		return FALSE;
	}

	gboolean doStartReceiver()
	{
		worker->start();
		return FALSE;
	}

	gboolean doStopProducer()
	{
		worker->stop();
		return FALSE;
	}

	gboolean doStopReceiver()
	{
		worker->stop();
		return FALSE;
	}

	void worker_started()
	{
		if(producerMode)
			emit producer_started();
		else
			emit receiver_started();
	}

	void worker_updated()
	{
		// TODO
	}

	void worker_stopped()
	{
		if(producerMode)
			emit producer_stopped();
		else
			emit receiver_stopped();
	}

	void worker_finished()
	{
		// TODO
	}

	void worker_error()
	{
		// TODO
	}

	void worker_previewFrame(const QImage &img)
	{
		QMutexLocker locker(render_mutex());
		if(!g_images)
			g_images = new QList<QImage>;
		g_images->append(img);
		QMetaObject::invokeMethod((QObject *)g_producer, "imageReady", Qt::QueuedConnection);
	}

	void worker_outputFrame(const QImage &img)
	{
		QMutexLocker locker(render_mutex());
		if(!g_rimages)
			g_rimages = new QList<QImage>;
		g_rimages->append(img);
		QMetaObject::invokeMethod((QObject *)g_receiver, "rimageReady", Qt::QueuedConnection);
	}

	void worker_rtpAudioOut(const PRtpPacket &packet)
	{
		QMutexLocker locker(in_mutex());
		if(!g_in_packets_audio)
			g_in_packets_audio = new QList<PRtpPacket>();
		if(g_in_packets_audio->count() < 5)
		{
			g_in_packets_audio->append(packet);
			QMetaObject::invokeMethod((QObject *)g_producer, "packetReadyAudio", Qt::QueuedConnection);
		}
	}

	void worker_rtpVideoOut(const PRtpPacket &packet)
	{
		QMutexLocker locker(in_mutex());
		if(!g_in_packets)
			g_in_packets = new QList<PRtpPacket>();
		if(g_in_packets->count() < 5)
		{
			g_in_packets->append(packet);
			QMetaObject::invokeMethod((QObject *)g_producer, "packetReady", Qt::QueuedConnection);
		}
	}
};

GstThread *GstThread::self = 0;

//----------------------------------------------------------------------------
// GstRtpChannel
//----------------------------------------------------------------------------
class GstProducerContext;

class GstRtpChannel : public QObject, public RtpChannelContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::RtpChannelContext)

public:
	friend class GstRtpSessionContext;
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
		receiver_write(this, rtp);
	}

signals:
	void readyRead();
	void packetsWritten(int count);
};

//----------------------------------------------------------------------------
// GstRtpSessionContext
//----------------------------------------------------------------------------
class GstRtpSessionContext : public QObject, public RtpSessionContext
{
	Q_OBJECT
	Q_INTERFACES(PsiMedia::RtpSessionContext)

public:
	QString audioOutId;
	QString audioInId, videoInId;
	QString fileIn;
	QByteArray fileDataIn;
	VideoWidgetContext *outputWidget, *previewWidget;
	int audioOutVolume;
	int audioInVolume;
	int code;

	GstRtpChannel audioRtp;
	GstRtpChannel videoRtp;

	// FIXME: remove this
	bool producerMode;

	GstRtpSessionContext(QObject *parent = 0) :
		QObject(parent),
		outputWidget(0),
		previewWidget(0),
		audioOutVolume(100),
		audioInVolume(100),
		code(-1)
	{
	}

	virtual QObject *qobject()
	{
		return this;
	}

	virtual void setAudioOutputDevice(const QString &deviceId)
	{
		audioOutId = deviceId;
		// TODO: if active, switch to that device
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
        virtual void setVideoOutputWidget(VideoWidgetContext *widget)
	{
		outputWidget = widget;
		// TODO: if active, switch to using (or not using)
	}

	virtual void setVideoPreviewWidget(VideoWidgetContext *widget)
	{
		previewWidget = widget;
		// TODO: if active, switch to using (or not using)
	}
#endif

	virtual void setRecorder(QIODevice *recordDevice)
	{
		// TODO
		Q_UNUSED(recordDevice);
	}

	virtual void setLocalAudioPreferences(const QList<PAudioParams> &params)
	{
		// TODO
		Q_UNUSED(params);
	}

	virtual void setLocalAudioPreferences(const QList<PPayloadInfo> &info)
	{
		// TODO
		Q_UNUSED(info);
	}

	virtual void setLocalVideoPreferences(const QList<PVideoParams> &params)
	{
		// TODO
		Q_UNUSED(params);
	}

	virtual void setLocalVideoPreferences(const QList<PPayloadInfo> &info)
	{
		// TODO
		Q_UNUSED(info);
	}

	virtual void setRemoteAudioPreferences(const QList<PPayloadInfo> &info)
	{
		// TODO
		GstThread::instance()->worker->remoteAudioPayloadInfo = info.first();
	}

	virtual void setRemoteVideoPreferences(const QList<PPayloadInfo> &info)
	{
		// TODO
		GstThread::instance()->worker->remoteVideoPayloadInfo = info.first();
	}

	virtual void start()
	{
		// TODO

		// probably producer
		if(!audioInId.isEmpty() || !videoInId.isEmpty() || !fileIn.isEmpty())
		{
			producerMode = true;
			GstThread::instance()->producerMode = producerMode;
			g_producer = this;

			connect(GstThread::instance(), SIGNAL(producer_started()), SIGNAL(started()));
			connect(GstThread::instance(), SIGNAL(producer_stopped()), SIGNAL(stopped()));

			GstThread::instance()->worker->ain = audioInId;
			GstThread::instance()->worker->vin = videoInId;
			GstThread::instance()->worker->infile = fileIn;
			GstThread::instance()->startProducer();
		}
		// receiver
		else
		{
			producerMode = false;
			GstThread::instance()->producerMode = producerMode;
			g_receiver = this;

			connect(GstThread::instance(), SIGNAL(receiver_started()), SIGNAL(started()));
			connect(GstThread::instance(), SIGNAL(receiver_stopped()), SIGNAL(stopped()));

			GstThread::instance()->worker->aout = audioOutId;
			GstThread::instance()->startReceiver();
		}
	}

	virtual void updatePreferences()
	{
		// TODO
	}

	virtual void transmitAudio(int index)
	{
		// TODO (note that -1 means pick best)
		Q_UNUSED(index);
	}

	virtual void transmitVideo(int index)
	{
		// TODO (note that -1 means pick best)
		Q_UNUSED(index);
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

		if(producerMode)
			GstThread::instance()->stopProducer();
		else
			GstThread::instance()->stopReceiver();
	}

	virtual QList<PPayloadInfo> audioPayloadInfo() const
	{
		// TODO
		return QList<PPayloadInfo>() << GstThread::instance()->worker->localAudioPayloadInfo;
	}

	virtual QList<PPayloadInfo> videoPayloadInfo() const
	{
		// TODO
		return QList<PPayloadInfo>() << GstThread::instance()->worker->localVideoPayloadInfo;
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

	virtual bool canTransmitAudio() const
	{
		// TODO
		return true;
	}

	virtual bool canTransmitVideo() const
	{
		// TODO
		return true;
	}

	virtual int outputVolume() const
	{
		return audioOutVolume;
	}

	virtual void setOutputVolume(int level)
	{
		audioOutVolume = level;
		// TODO: if active, change active volume
	}

	virtual int inputVolume() const
	{
		return audioInVolume;
	}

	virtual void setInputVolume(int level)
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
	void preferencesUpdated();
	void audioInputIntensityChanged(int intensity);
	void stopped();
	void finished();
	void error();

public:
	void doWrite(GstRtpChannel *from, const PRtpPacket &rtp)
	{
		if(from == &audioRtp)
			GstThread::instance()->worker->rtpAudioIn(rtp);
		else if(from == &videoRtp)
			GstThread::instance()->worker->rtpVideoIn(rtp);
	}

public slots:
	void imageReady()
	{
		render_mutex()->lock();
		QImage image = g_images->takeFirst();
		render_mutex()->unlock();

		if(previewWidget)
			previewWidget->show_frame(image);
	}

	void packetReadyAudio()
	{
		in_mutex()->lock();
		PRtpPacket packet = g_in_packets_audio->takeFirst();
		in_mutex()->unlock();

		//printf("audio packet ready (%d bytes)\n", packet.rawValue.size());
		audioRtp.in += packet;
		emit audioRtp.readyRead();
	}

	void packetReady()
	{
		in_mutex()->lock();
		PRtpPacket packet = g_in_packets->takeFirst();
		in_mutex()->unlock();

		//printf("video packet ready\n");
		videoRtp.in += packet;
		emit videoRtp.readyRead();
	}

	void rimageReady()
	{
		render_mutex()->lock();
		QImage image = g_rimages->takeFirst();
		render_mutex()->unlock();

		if(outputWidget)
			outputWidget->show_frame(image);
	}
};

void receiver_write(GstRtpChannel *from, const PRtpPacket &rtp)
{
	if(g_receiver)
		g_receiver->doWrite(from, rtp);
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
		thread->start(resourcePath);
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
		QString str =
		"This application uses GStreamer, a comprehensive "
		"open-source and cross-platform multimedia framework.  For "
		"more information, see http://www.gstreamer.net/\n\n"
		"If you enjoy this software, please give the GStreamer "
		"people a million dollars.";
		return str;
	}

	// FIXME: any better way besides hardcoding?
	virtual QList<PAudioParams> supportedAudioModes()
	{
		QList<PAudioParams> list;
		{
			PAudioParams p;
			p.codec = "pcmu";
			p.sampleRate = 8000;
			p.sampleSize = 16;
			p.channels = 1;
			list += p;
		}
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
		{
			PAudioParams p;
			p.codec = "vorbis";
			p.sampleRate = 44100;
			p.sampleSize = 16;
			p.channels = 2;
			list += p;
		}
		return list;
	}

	// FIXME: any better way besides hardcoding?
	virtual QList<PVideoParams> supportedVideoModes()
	{
		QList<PVideoParams> list;
		{
			PVideoParams p;
			p.codec = "h263p";
			p.size = QSize(160, 120);
			p.fps = 15;
			list += p;
		}
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
		{
			PVideoParams p;
			p.codec = "theora";
			p.size = QSize(640, 480);
			p.fps = 15;
			list += p;
		}
		{
			PVideoParams p;
			p.codec = "theora";
			p.size = QSize(640, 480);
			p.fps = 30;
			list += p;
		}
		return list;
	}

	virtual QList<PDevice> audioOutputDevices()
	{
		QList<PDevice> list;
		foreach(const GstDevice &i, gstAudioOutputDevices())
			list += gstDeviceToPDevice(i, PDevice::AudioOut);
		return list;
	}

	virtual QList<PDevice> audioInputDevices()
	{
		QList<PDevice> list;
		foreach(const GstDevice &i, gstAudioInputDevices())
			list += gstDeviceToPDevice(i, PDevice::AudioIn);
		return list;
	}

	virtual QList<PDevice> videoInputDevices()
	{
		QList<PDevice> list;
		foreach(const GstDevice &i, gstVideoInputDevices())
			list += gstDeviceToPDevice(i, PDevice::VideoIn);
		return list;
	}

	virtual RtpSessionContext *createRtpSession()
	{
		return new GstRtpSessionContext;
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
