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

#include "gstthread.h"

#include <QStringList>
#include <QDir>
#include <QLibrary>
#include <QCoreApplication>
#include <QMutex>
#include <QWaitCondition>
#include <gst/gst.h>
#include "gstcustomelements/gstcustomelements.h"
#include "gstelements/static/gstelements.h"

namespace PsiMedia {

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
				delete [] data[n];
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
			filePath.toUtf8().data(), &err);
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

static int compare_gst_version(int a1, int a2, int a3, int b1, int b2, int b3)
{
	if(a1 > b1)
		return 1;
	else if(a1 < b1)
		return -1;

	if(a2 > b2)
		return 1;
	else if(a2 < b2)
		return -1;

	if(a3 > b3)
		return 1;
	else if(a3 < b3)
		return -1;

	return 0;
}

class GstSession
{
public:
	CArgs args;
	QString version;
	bool success;

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
			success = false;
			return;
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

		int need_maj = 0;
		int need_min = 10;
		int need_mic = 22;
		if(compare_gst_version(major, minor, micro, need_maj, need_min, need_mic) < 0)
		{
			printf("Need GStreamer version %d.%d.%d\n", need_maj, need_min, need_mic);
			success = false;
			return;
		}

		// manually load plugins?
		if(!pluginPath.isEmpty())
		{
			// ignore "system" plugins
			qputenv("GST_PLUGIN_PATH", "");

			loadPlugins(pluginPath);
		}

		gstcustomelements_register();
		gstelements_register();

		QStringList reqelem = QStringList()
			<< "speexenc" << "speexdec"
			<< "vorbisenc" << "vorbisdec"
			<< "theoraenc" << "theoradec"
			<< "rtpspeexpay" << "rtpspeexdepay"
			<< "rtpvorbispay" << "rtpvorbisdepay"
			<< "rtptheorapay" << "rtptheoradepay"
			<< "filesrc"
			<< "decodebin"
			<< "jpegdec"
			<< "oggmux" << "oggdemux"
			<< "audioconvert"
			<< "audioresample"
			<< "volume"
			<< "level"
			<< "ffmpegcolorspace"
			<< "videorate"
			<< "videomaxrate"
			<< "videoscale"
			<< "gstrtpjitterbuffer"
			<< "liveadder";

#if defined(Q_OS_MAC)
			reqelem
			<< "osxaudiosrc" << "osxaudiosink"
			<< "osxvideosrc";
#elif defined(Q_OS_LINUX)
			reqelem
			<< "alsasrc" << "alsasink"
			<< "v4lsrc"
			<< "v4l2src";
#elif defined(Q_OS_UNIX)
			reqelem
			<< "osssrc" << "osssink";
#elif defined(Q_OS_WIN)
			reqelem
			<< "directsoundsrc" << "directsoundsink"
			<< "ksvideosrc";
#endif

		foreach(const QString &name, reqelem)
		{
			GstElement *e = gst_element_factory_make(name.toLatin1().data(), NULL);
			if(!e)
			{
				printf("Unable to load element '%s'.\n", qPrintable(name));
				success = false;
				return;
			}

			g_object_unref(G_OBJECT(e));
		}

		success = true;
	}

	~GstSession()
	{
		// nothing i guess.  docs say to not bother with gst_deinit
		//gst_deinit();
	}
};

//----------------------------------------------------------------------------
// GstThread
//----------------------------------------------------------------------------
class GstThread::Private
{
public:
	QString pluginPath;
	GstSession *gstSession;
	bool success;
	GMainContext *mainContext;
	GMainLoop *mainLoop;
	QMutex m;
	QWaitCondition w;

	Private() :
		mainContext(0),
		mainLoop(0)
	{
	}

	static gboolean cb_loop_started(gpointer data)
	{
		return ((Private *)data)->loop_started();
	}

	gboolean loop_started()
	{
		w.wakeOne();
		m.unlock();
		return FALSE;
	}
};

GstThread::GstThread(QObject *parent) :
	QThread(parent)
{
	d = new Private;
}

GstThread::~GstThread()
{
	stop();
	delete d;
}

bool GstThread::start(const QString &pluginPath)
{
	QMutexLocker locker(&d->m);
	d->pluginPath = pluginPath;
	QThread::start();
	d->w.wait(&d->m);
	return d->success;
}

void GstThread::stop()
{
	QMutexLocker locker(&d->m);
	if(d->mainLoop)
	{
		// thread-safe ?
		g_main_loop_quit(d->mainLoop);
		d->w.wait(&d->m);
	}

	wait();
}

QString GstThread::gstVersion() const
{
	QMutexLocker locker(&d->m);
	return d->gstSession->version;
}

GMainContext *GstThread::mainContext()
{
	QMutexLocker locker(&d->m);
	return d->mainContext;
}

void GstThread::run()
{
	//printf("GStreamer thread started\n");

	// this will be unlocked as soon as the mainloop runs
	d->m.lock();

	d->gstSession = new GstSession(d->pluginPath);

	// report error
	if(!d->gstSession->success)
	{
		d->success = false;
		delete d->gstSession;
		d->gstSession = 0;
		d->w.wakeOne();
		d->m.unlock();
		//printf("GStreamer thread completed (error)\n");
		return;
	}

	d->success = true;

	//printf("Using GStreamer version %s\n", qPrintable(d->gstSession->version));

	d->mainContext = g_main_context_new();
	d->mainLoop = g_main_loop_new(d->mainContext, FALSE);

	// deferred call to loop_started()
	GSource *timer = g_timeout_source_new(0);
	g_source_attach(timer, d->mainContext);
	g_source_set_callback(timer, GstThread::Private::cb_loop_started, d, NULL);

	// kick off the event loop
	g_main_loop_run(d->mainLoop);

	QMutexLocker locker(&d->m);
	g_main_loop_unref(d->mainLoop);
	d->mainLoop = 0;
	g_main_context_unref(d->mainContext);
	d->mainContext = 0;
	delete d->gstSession;
	d->gstSession = 0;

	d->w.wakeOne();
	//printf("GStreamer thread completed\n");
}

}
