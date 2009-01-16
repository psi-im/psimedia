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

#ifndef RTPWORKER_H
#define RTPWORKER_H

#include <QString>
#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <gst/gst.h>
#include "psimediaprovider.h"
#include "gstcustomelements/gstcustomelements.h"

namespace PsiMedia {

// Note: do not destruct this class during one of its callbacks
class RtpWorker
{
public:
	// this class exists in case we want to add metadata to the image,
	//   such as a timestamp
	class Frame
	{
	public:
		QImage image;
	};

	void *app; // for callbacks

	QString aout;
	QString ain;
	QString vin;
	QString infile;
	QByteArray indata;
	bool loopFile;
	QList<PAudioParams> localAudioParams;
	QList<PVideoParams> localVideoParams;
	QList<PPayloadInfo> localAudioPayloadInfo;
	QList<PPayloadInfo> localVideoPayloadInfo;
	QList<PPayloadInfo> remoteAudioPayloadInfo;
	QList<PPayloadInfo> remoteVideoPayloadInfo;

	// read-only
	bool canTransmitAudio;
	bool canTransmitVideo;
	int outputVolume;
	int inputVolume;
	int error;

	RtpWorker(GMainContext *mainContext);
	~RtpWorker();

	void start();
	void update();
	void transmitAudio(int index);
	void transmitVideo(int index);
	void pauseAudio();
	void pauseVideo();
	void stop();

	// the rtp input functions are safe to call from any thread
	void rtpAudioIn(const PRtpPacket &packet);
	void rtpVideoIn(const PRtpPacket &packet);

	void setOutputVolume(int level);
	void setInputVolume(int level);

	void recordStart();
	void recordStop();

	// callbacks

	void (*cb_started)(void *app);
	void (*cb_updated)(void *app);
	void (*cb_stopped)(void *app);
	void (*cb_finished)(void *app);
	void (*cb_error)(void *app);
	void (*cb_audioIntensity)(int value, void *app);

	// callbacks - from alternate thread, be safe!
	//   also, it is not safe to assign callbacks except before starting

	void (*cb_previewFrame)(const Frame &frame, void *app);
	void (*cb_outputFrame)(const Frame &frame, void *app);
	void (*cb_rtpAudioOut)(const PRtpPacket &packet, void *app);
	void (*cb_rtpVideoOut)(const PRtpPacket &packet, void *app);

	 // empty record packet = EOF/error
	void (*cb_recordData)(const QByteArray &packet, void *app);

private:
	GMainContext *mainContext_;
	GSource *timer;

	GstElement *sendPipeline, *rpipeline, *rvpipeline;
	GstElement *fileDemux;
	GstElement *audiosrc;
	GstElement *videosrc;
	GstElement *audiortpsrc;
	GstElement *videortpsrc;
	GstElement *audiortppay;
	GstElement *videortppay;
	GstElement *volumein;
	GstElement *volumeout;
	bool rtpaudioout;
	bool rtpvideoout;
	QMutex audiortpsrc_mutex;
	QMutex videortpsrc_mutex;
	QMutex volumein_mutex;
	QMutex volumeout_mutex;
	QMutex rtpaudioout_mutex;
	QMutex rtpvideoout_mutex;

	GSource *recordTimer;

	void cleanup();

	static gboolean cb_doStart(gpointer data);
	static gboolean cb_doUpdate(gpointer data);
	static gboolean cb_doStop(gpointer data);
	static void cb_fileDemux_no_more_pads(GstElement *element, gpointer data);
	static void cb_fileDemux_pad_added(GstElement *element, GstPad *pad, gpointer data);
	static void cb_fileDemux_pad_removed(GstElement *element, GstPad *pad, gpointer data);
	static gboolean cb_bus_call(GstBus *bus, GstMessage *msg, gpointer data);
	static void cb_show_frame_preview(int width, int height, const unsigned char *rgb32, gpointer data);
	static void cb_show_frame_output(int width, int height, const unsigned char *rgb32, gpointer data);
	static void cb_packet_ready_rtp_audio(const unsigned char *buf, int size, gpointer data);
	static void cb_packet_ready_rtp_video(const unsigned char *buf, int size, gpointer data);
	static gboolean cb_fileReady(gpointer data);

	gboolean doStart();
	gboolean doUpdate();
	gboolean doStop();
	void fileDemux_no_more_pads(GstElement *element);
	void fileDemux_pad_added(GstElement *element, GstPad *pad);
	void fileDemux_pad_removed(GstElement *element, GstPad *pad);
	gboolean bus_call(GstBus *bus, GstMessage *msg);
	void show_frame_preview(int width, int height, const unsigned char *rgb32);
	void show_frame_output(int width, int height, const unsigned char *rgb32);
	void packet_ready_rtp_audio(const unsigned char *buf, int size);
	void packet_ready_rtp_video(const unsigned char *buf, int size);
	gboolean fileReady();

	bool addAudioChain();
	bool addVideoChain();
	bool getCaps();
};

}

#endif
