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

#ifndef RWCONTROL_H
#define RWCONTROL_H

#include <QString>
#include <QList>
#include <QByteArray>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <glib/gmain.h>
#include "psimediaprovider.h"
#include "rtpworker.h"

namespace PsiMedia {

// These classes allow controlling RtpWorker from across the qt<->glib
// thread boundary.
//
// RwControlLocal  - object to live in "local" Qt eventloop
// RwControlRemote - object to live in "remote" glib eventloop
//
// When RwControlLocal is created, you pass it the GstThread.  It will then
// atomically create RwControlRemote in the remote thread and associate the
// two objects.
//
// The possible exchanges are made clear here.  Things you can do:
//
// - Start a session.  This requires device and codec configuration to begin.
//   This operation is a transaction, you'll receive a status message when it
//   completes.
//
// - Stop a session.  This operation is a transaction, you'll receive a
//   status message when it completes.
//
// - Update complete device configuration.  This is fire and forget.
//   Eventually it will take effect, and you won't be notified when it
//   happens.  From a local standpoint you simply assume it took effect
//   immediately.
//
// - Update codec configuration.  This is a transaction, you'll receive a
//   status message when it completes.
//
// - Transmit/pause the audio/video streams.  This is fire and forget.
//
// - Start/stop recording a session.  For starting, this is somewhat fire
//   and forget.  You'll eventually start receiving data packets, but the
//   assumption is that recording is occurring even before the first packet
//   is received.  For stopping, this is somewhat transactional.  The record
//   is not considered stopped until an EOF packet is received.
//
// - At any time, it is possible to receive a spontaneous status message.
//   This is to indicate an error or a completed file playback.
//
// - Preview and output video frames are signaled normally and are intended
//   for immediate display.
//
// - RTP packets and recording data bypass the event-based message-passing
//   mechanisms described above.  Instead, special methods and callbacks are
//   used which require special care.

class GstThread;
class RwControlRemote;

class RwControlConfigDevices
{
public:
	QString audioOutId;
	QString audioInId;
	QString videoInId;
	QString fileNameIn;
	QByteArray fileDataIn;
	bool loopFile;
	int audioOutVolume;
	int audioInVolume;

	RwControlConfigDevices() :
		loopFile(false),
		audioOutVolume(-1),
		audioInVolume(-1)
	{
	}
};

class RwControlConfigCodecs
{
public:
	bool useLocalParams;
	bool useLocalPayloadInfo;
	bool useRemotePayloadInfo;

	QList<PAudioParams> localAudioParams;
	QList<PVideoParams> localVideoParams;
	QList<PPayloadInfo> localAudioPayloadInfo;
	QList<PPayloadInfo> localVideoPayloadInfo;
	QList<PPayloadInfo> remoteAudioPayloadInfo;
	QList<PPayloadInfo> remoteVideoPayloadInfo;

	RwControlConfigCodecs() :
		useLocalParams(false),
		useLocalPayloadInfo(false),
		useRemotePayloadInfo(false)
	{
	}
};

class RwControlTransmit
{
public:
	int audioIndex;
	int videoIndex;

	RwControlTransmit() :
		audioIndex(-1),
		videoIndex(-1)
	{
	}
};

class RwControlRecord
{
public:
	bool enabled;

	RwControlRecord() :
		enabled(false)
	{
	}
};

// note: if this is received spontaneously, then only finished, error, and
//   errorCode are valid
class RwControlStatus
{
public:
	QList<PAudioParams> localAudioParams;
	QList<PVideoParams> localVideoParams;
	QList<PPayloadInfo> localAudioPayloadInfo;
	QList<PPayloadInfo> localVideoPayloadInfo;
	bool canTransmitAudio;
	bool canTransmitVideo;

	bool stopped;
	bool finished;
	bool error;
	RtpSessionContext::Error errorCode;

	RwControlStatus() :
		canTransmitAudio(false),
		canTransmitVideo(false),
		stopped(false),
		finished(false),
		error(false),
		errorCode(RtpSessionContext::ErrorGeneric)
	{
	}
};

// generalize the messages
class RwControlMessage
{
public:
	enum Type
	{
		Start,
		Stop,
		UpdateDevices,
		UpdateCodecs,
		Transmit,
		Record,
		Status
	};

	Type type;

	// a message can contain multiple data elements
	RwControlConfigDevices *devs;
	RwControlConfigCodecs *codecs;
	RwControlTransmit *transmit;
	RwControlRecord *record;
	RwControlStatus *status;

	RwControlMessage() :
		type((Type)-1),
		devs(0),
		codecs(0),
		transmit(0),
		record(0),
		status(0)
	{
	}

	~RwControlMessage()
	{
		delete devs;
		delete codecs;
		delete transmit;
		delete record;
		delete status;
	}
};

class RwControlLocal : public QObject
{
	Q_OBJECT

public:
	RwControlLocal(GstThread *thread, QObject *parent = 0);
	~RwControlLocal();

	void start(const RwControlConfigDevices &devs, const RwControlConfigCodecs &codecs);
	void stop();
	void updateDevices(const RwControlConfigDevices &devs);
	void updateCodecs(const RwControlConfigCodecs &codecs);
	void setTransmit(const RwControlTransmit &transmit);
	void setRecord(const RwControlRecord &record);

	// can be called from any thread
	void rtpAudioIn(const PRtpPacket &packet);
	void rtpVideoIn(const PRtpPacket &packet);

	// can come from any thread
	void (*cb_rtpAudioOut)(const PRtpPacket &packet, void *app);
	void (*cb_rtpVideoOut)(const PRtpPacket &packet, void *app);
	void (*cb_recordData)(const QByteArray &packet, void *app);

signals:
	// response to start, stop, updateCodecs, or it could be spontaneous
	void statusReady(const RwControlStatus &status);

	void previewFrame(const QImage &img);
	void outputFrame(const QImage &img);

private slots:
	void processMessages();

private:
	GstThread *thread_;
	GSource *timer;
	QMutex m;
	QWaitCondition w;
	RwControlRemote *remote_;
	QTimer processTrigger;

	QList<RwControlMessage*> in;

	static gboolean cb_doCreateRemote(gpointer data);
	static gboolean cb_doDestroyRemote(gpointer data);

	gboolean doCreateRemote();
	gboolean doDestroyRemote();

	friend class RwControlRemote;
	void postMessage(RwControlMessage *msg);
};

class RwControlRemote
{
public:
	RwControlRemote(GMainContext *mainContext, RwControlLocal *local);
	~RwControlRemote();

private:
	GSource *timer;
	GMainContext *mainContext_;
	QMutex m;
	RwControlLocal *local_;

	RtpWorker *worker;
	QList<RwControlMessage*> in;

	static gboolean cb_processMessages(gpointer data);
	static void cb_worker_started(void *app);
	static void cb_worker_updated(void *app);
	static void cb_worker_stopped(void *app);
	static void cb_worker_finished(void *app);
	static void cb_worker_error(void *app);
	static void cb_worker_previewFrame(const RtpWorker::Frame &frame, void *app);
	static void cb_worker_outputFrame(const RtpWorker::Frame &frame, void *app);
	static void cb_worker_rtpAudioOut(const PRtpPacket &packet, void *app);
	static void cb_worker_rtpVideoOut(const PRtpPacket &packet, void *app);
	static void cb_worker_recordData(const QByteArray &packet, void *app);

	gboolean processMessages();
	void worker_started();
	void worker_updated();
	void worker_stopped();
	void worker_finished();
	void worker_error();
	void worker_previewFrame(const RtpWorker::Frame &frame);
	void worker_outputFrame(const RtpWorker::Frame &frame);
	void worker_rtpAudioOut(const PRtpPacket &packet);
	void worker_rtpVideoOut(const PRtpPacket &packet);
	void worker_recordData(const QByteArray &packet);

	friend class RwControlLocal;
	void postMessage(RwControlMessage *msg);
};

}

#endif
