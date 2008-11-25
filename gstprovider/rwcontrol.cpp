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

#include "rwcontrol.h"

#include "gstthread.h"
#include "rtpworker.h"

namespace PsiMedia {

//----------------------------------------------------------------------------
// RwControlLocal
//----------------------------------------------------------------------------
RwControlLocal::RwControlLocal(GstThread *thread, QObject *parent) :
	QObject(parent),
	cb_rtpAudioOut(0),
	cb_rtpVideoOut(0),
	cb_recordData(0),
	processTrigger(this)
{
	thread_ = thread;
	remote_ = 0;

	connect(&processTrigger, SIGNAL(timeout()), SLOT(processMessages()));
	processTrigger.setSingleShot(true);

	// create RwControlRemote, block until ready
	QMutexLocker locker(&m);
	timer = g_timeout_source_new(0);
	g_source_set_callback(timer, cb_doCreateRemote, this, NULL);
	g_source_attach(timer, thread_->mainContext());
	w.wait(&m);
}

RwControlLocal::~RwControlLocal()
{
	qDeleteAll(in);

	// delete RwControlRemote, block until done
	QMutexLocker locker(&m);
	timer = g_timeout_source_new(0);
	g_source_set_callback(timer, cb_doDestroyRemote, this, NULL);
	g_source_attach(timer, thread_->mainContext());
	w.wait(&m);
}

void RwControlLocal::start(const RwControlConfigDevices &devs, const RwControlConfigCodecs &codecs)
{
	RwControlMessage *msg = new RwControlMessage;
	msg->type = RwControlMessage::Start;
	msg->devs = new RwControlConfigDevices(devs);
	msg->codecs = new RwControlConfigCodecs(codecs);
	remote_->postMessage(msg);
}

void RwControlLocal::stop()
{
}

void RwControlLocal::updateDevices(const RwControlConfigDevices &devs)
{
	// TODO
	Q_UNUSED(devs);
}

void RwControlLocal::updateCodecs(const RwControlConfigCodecs &codecs)
{
	// TODO
	Q_UNUSED(codecs);
}

void RwControlLocal::setTransmit(const RwControlTransmit &transmit)
{
	// TODO
	Q_UNUSED(transmit);
}

void RwControlLocal::setRecord(const RwControlRecord &record)
{
	// TODO
	Q_UNUSED(record);
}

void RwControlLocal::rtpAudioIn(const PRtpPacket &packet)
{
	// TODO
	Q_UNUSED(packet);
}

void RwControlLocal::rtpVideoIn(const PRtpPacket &packet)
{
	// TODO
	Q_UNUSED(packet);
}

// note: this is executed in the remote thread
gboolean RwControlLocal::cb_doCreateRemote(gpointer data)
{
	return ((RwControlLocal *)data)->doCreateRemote();
}

// note: this is executed in the remote thread
gboolean RwControlLocal::doCreateRemote()
{
	QMutexLocker locker(&m);
	timer = 0;
	remote_ = new RwControlRemote(thread_->mainContext(), this);
	w.wakeOne();
	return FALSE;
}

// note: this is executed in the remote thread
gboolean RwControlLocal::cb_doDestroyRemote(gpointer data)
{
	return ((RwControlLocal *)data)->doDestroyRemote();
}

// note: this is executed in the remote thread
gboolean RwControlLocal::doDestroyRemote()
{
	QMutexLocker locker(&m);
	timer = 0;
	delete remote_;
	remote_ = 0;
	w.wakeOne();
	return FALSE;
}

void RwControlLocal::processMessages()
{
	while(1)
	{
		m.lock();
		if(in.isEmpty())
		{
			m.unlock();
			break;
		}

		RwControlMessage *msg = in.takeFirst();
		m.unlock();

		// remote only ever sends status type
		Q_ASSERT(msg->type == RwControlMessage::Status);

		RwControlStatus status = *(msg->status);
		delete msg;
		emit statusReady(status);

		// FIXME: signal-safety (due to loop)
	}
}

// note: this may be called from the remote thread
void RwControlLocal::postMessage(RwControlMessage *msg)
{
	QMutexLocker locker(&m);
	in += msg;
	if(!processTrigger.isActive())
		processTrigger.start();
}

//----------------------------------------------------------------------------
// RwControlRemote
//----------------------------------------------------------------------------
RwControlRemote::RwControlRemote(GMainContext *mainContext, RwControlLocal *local) :
	timer(0)
{
	mainContext_ = mainContext;
	local_ = local;
	worker = new RtpWorker(mainContext_);
	worker->app = this;
	worker->cb_started = cb_worker_started;
	worker->cb_updated = cb_worker_updated;
	worker->cb_stopped = cb_worker_stopped;
	worker->cb_finished = cb_worker_finished;
	worker->cb_error = cb_worker_error;
	worker->cb_previewFrame = cb_worker_previewFrame;
	worker->cb_outputFrame = cb_worker_outputFrame;
	worker->cb_rtpAudioOut = cb_worker_rtpAudioOut;
	worker->cb_rtpVideoOut = cb_worker_rtpVideoOut;
	worker->cb_recordData = cb_worker_recordData;
}

RwControlRemote::~RwControlRemote()
{
	qDeleteAll(in);

	delete worker;
}

gboolean RwControlRemote::cb_processMessages(gpointer data)
{
	return ((RwControlRemote *)data)->processMessages();
}

void RwControlRemote::cb_worker_started(void *app)
{
	((RwControlRemote *)app)->worker_started();
}

void RwControlRemote::cb_worker_updated(void *app)
{
	((RwControlRemote *)app)->worker_updated();
}

void RwControlRemote::cb_worker_stopped(void *app)
{
	((RwControlRemote *)app)->worker_stopped();
}

void RwControlRemote::cb_worker_finished(void *app)
{
	((RwControlRemote *)app)->worker_finished();
}

void RwControlRemote::cb_worker_error(void *app)
{
	((RwControlRemote *)app)->worker_error();
}

void RwControlRemote::cb_worker_previewFrame(const RtpWorker::Frame &frame, void *app)
{
	((RwControlRemote *)app)->worker_previewFrame(frame);
}

void RwControlRemote::cb_worker_outputFrame(const RtpWorker::Frame &frame, void *app)
{
	((RwControlRemote *)app)->worker_outputFrame(frame);
}

void RwControlRemote::cb_worker_rtpAudioOut(const PRtpPacket &packet, void *app)
{
	((RwControlRemote *)app)->worker_rtpAudioOut(packet);
}

void RwControlRemote::cb_worker_rtpVideoOut(const PRtpPacket &packet, void *app)
{
	((RwControlRemote *)app)->worker_rtpVideoOut(packet);
}

void RwControlRemote::cb_worker_recordData(const QByteArray &packet, void *app)
{
	((RwControlRemote *)app)->worker_recordData(packet);
}

gboolean RwControlRemote::processMessages()
{
	timer = 0;

	while(1)
	{
		m.lock();
		if(in.isEmpty())
		{
			m.unlock();
			break;
		}

		RwControlMessage *msg = in.takeFirst();
		m.unlock();

		if(msg->type == RwControlMessage::Start)
		{
			worker->start();
		}

		delete msg;
	}

	return FALSE;
}

void RwControlRemote::worker_started()
{
	RwControlMessage *msg = new RwControlMessage;
	msg->type = RwControlMessage::Status;
	msg->status = new RwControlStatus;
	local_->postMessage(msg);
}

void RwControlRemote::worker_updated()
{
	// TODO
}

void RwControlRemote::worker_stopped()
{
	// TODO
}

void RwControlRemote::worker_finished()
{
	// TODO
}

void RwControlRemote::worker_error()
{
	// TODO
}

void RwControlRemote::worker_previewFrame(const RtpWorker::Frame &frame)
{
	// TODO
	Q_UNUSED(frame);
}

void RwControlRemote::worker_outputFrame(const RtpWorker::Frame &frame)
{
	// TODO
	Q_UNUSED(frame);
}

void RwControlRemote::worker_rtpAudioOut(const PRtpPacket &packet)
{
	// TODO
	Q_UNUSED(packet);
}

void RwControlRemote::worker_rtpVideoOut(const PRtpPacket &packet)
{
	// TODO
	Q_UNUSED(packet);
}

void RwControlRemote::worker_recordData(const QByteArray &packet)
{
	// TODO
	Q_UNUSED(packet);
}

// note: this may be called from the local thread
void RwControlRemote::postMessage(RwControlMessage *msg)
{
	QMutexLocker locker(&m);
	in += msg;
	if(!timer)
	{
		timer = g_timeout_source_new(0);
		g_source_set_callback(timer, cb_processMessages, this, NULL);
		g_source_attach(timer, mainContext_);
	}
}

}
