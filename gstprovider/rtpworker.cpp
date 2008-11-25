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

#include "rtpworker.h"

#include <QStringList>
#include "devices.h"
#include "payloadinfo.h"

namespace PsiMedia {

static GstElement *make_device_element(const QString &id, PDevice::Type type, QSize *captureSize = 0)
{
	return devices_makeElement(id, type, captureSize);
}

/*static void nullAndUnrefElements(QList<GstElement*> &in)
{
	foreach(GstElement *e, in)
	{
		gst_element_set_state(e, GST_STATE_NULL);
		g_object_unref(G_OBJECT(e));
	}
}*/

RtpWorker::RtpWorker(GMainContext *mainContext) :
	loopFile(false),
	cb_started(0),
	cb_updated(0),
	cb_stopped(0),
	cb_finished(0),
	cb_error(0),
	cb_previewFrame(0),
	cb_outputFrame(0),
	cb_rtpAudioOut(0),
	cb_rtpVideoOut(0),
	mainContext_(mainContext),
	timer(0),
	pipeline(0)
{
}

RtpWorker::~RtpWorker()
{
	if(timer)
		g_source_destroy(timer);
}

void RtpWorker::start()
{
	Q_ASSERT(!timer);
	timer = g_timeout_source_new(0);
	g_source_set_callback(timer, cb_doStart, this, NULL);
	g_source_attach(timer, mainContext_);
}

void RtpWorker::update()
{
	// TODO
}

void RtpWorker::transmitAudio(int index)
{
	// TODO
	Q_UNUSED(index);
}

void RtpWorker::transmitVideo(int index)
{
	// TODO
	Q_UNUSED(index);
}

void RtpWorker::pauseAudio()
{
	// TODO
}

void RtpWorker::pauseVideo()
{
	// TODO
}

void RtpWorker::stop()
{
	Q_ASSERT(!timer);
	timer = g_timeout_source_new(0);
	g_source_set_callback(timer, cb_doStop, this, NULL);
	g_source_attach(timer, mainContext_);
}

void RtpWorker::rtpAudioIn(const PRtpPacket &packet)
{
	if(packet.portOffset == 0)
		gst_apprtpsrc_packet_push((GstAppRtpSrc *)audiortpsrc, (const unsigned char *)packet.rawValue.data(), packet.rawValue.size());
}

void RtpWorker::rtpVideoIn(const PRtpPacket &packet)
{
	if(packet.portOffset == 0)
		gst_apprtpsrc_packet_push((GstAppRtpSrc *)videortpsrc, (const unsigned char *)packet.rawValue.data(), packet.rawValue.size());
}

void RtpWorker::setOutputVolume(int level)
{
	// TODO
	Q_UNUSED(level);
}

void RtpWorker::setInputVolume(int level)
{
	// TODO
	Q_UNUSED(level);
}

void RtpWorker::recordStart()
{
	// TODO
}

void RtpWorker::recordStop()
{
	// TODO
}

gboolean RtpWorker::cb_doStart(gpointer data)
{
	return ((RtpWorker *)data)->doStart();
}

gboolean RtpWorker::cb_doStop(gpointer data)
{
	return ((RtpWorker *)data)->doStop();
}

void RtpWorker::cb_fileDemux_no_more_pads(GstElement *element, gpointer data)
{
	((RtpWorker *)data)->fileDemux_no_more_pads(element);
}

void RtpWorker::cb_fileDemux_pad_added(GstElement *element, GstPad *pad, gpointer data)
{
	((RtpWorker *)data)->fileDemux_pad_added(element, pad);
}

void RtpWorker::cb_fileDemux_pad_removed(GstElement *element, GstPad *pad, gpointer data)
{
	((RtpWorker *)data)->fileDemux_pad_removed(element, pad);
}

gboolean RtpWorker::cb_bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
	return ((RtpWorker *)data)->bus_call(bus, msg);
}

void RtpWorker::cb_show_frame_preview(int width, int height, const unsigned char *rgb24, gpointer data)
{
	((RtpWorker *)data)->show_frame_preview(width, height, rgb24);
}

void RtpWorker::cb_show_frame_output(int width, int height, const unsigned char *rgb24, gpointer data)
{
	((RtpWorker *)data)->show_frame_output(width, height, rgb24);
}

void RtpWorker::cb_packet_ready_rtp_audio(const unsigned char *buf, int size, gpointer data)
{
	((RtpWorker *)data)->packet_ready_rtp_audio(buf, size);
}

void RtpWorker::cb_packet_ready_rtp_video(const unsigned char *buf, int size, gpointer data)
{
	((RtpWorker *)data)->packet_ready_rtp_video(buf, size);
}

gboolean RtpWorker::doStart()
{
	// probably producer
	if(!ain.isEmpty() || !vin.isEmpty() || !infile.isEmpty())
	{
		producerMode = true;
		//g_producer = this;
	}
	else
	{
		producerMode = false;
		//g_receiver = this;
	}

	if(!producerMode)
		goto dorecv;

	{

	pipeline = gst_pipeline_new(NULL);

	GstElement *audioin = 0;
	GstElement *videoin = 0;
	fileSource = 0;
	GstCaps *videoincaps = 0;

	if(!infile.isEmpty())
	{
		printf("creating filesrc\n");

		fileSource = gst_element_factory_make("filesrc", NULL);
		g_object_set(G_OBJECT(fileSource), "location", infile.toLatin1().data(), NULL);

		fileDemux = gst_element_factory_make("oggdemux", NULL);
		g_signal_connect(G_OBJECT(fileDemux),
			"no-more-pads",
			G_CALLBACK(cb_fileDemux_no_more_pads), this);
		g_signal_connect(G_OBJECT(fileDemux),
			"pad-added",
			G_CALLBACK(cb_fileDemux_pad_added), this);
		g_signal_connect(G_OBJECT(fileDemux),
			"pad-removed",
			G_CALLBACK(cb_fileDemux_pad_removed), this);
	}
	else
	{
		if(!ain.isEmpty())
		{
			printf("creating audioin\n");

			audioin = make_device_element(ain, PDevice::AudioIn);
			if(!audioin)
			{
				// TODO
				printf("failed to create audio input element\n");
			}
		}

		if(!vin.isEmpty())
		{
			printf("creating videoin\n");

			QSize captureSize;
			videoin = make_device_element(vin, PDevice::VideoIn, &captureSize);
			if(!videoin)
			{
				// TODO
				printf("failed to create video input element\n");
			}

			if(captureSize.isValid())
			{
				videoincaps = gst_caps_new_simple("video/x-raw-yuv",
					"width", G_TYPE_INT, captureSize.width(),
					"height", G_TYPE_INT, captureSize.height(), NULL);
			}
		}
	}

	GstElement *audioqueue = 0, *audioconvert = 0, *audioresample = 0, *audioenc = 0, *audiortppay = 0, *audiortpsink = 0;
	GstAppRtpSink *appRtpSink = 0;
	GstCaps *caps5 = 0;

	if(audioin || fileSource)
	{
		audioqueue = gst_element_factory_make("queue", NULL);
		audioconvert = gst_element_factory_make("audioconvert", NULL);
		audioresample = gst_element_factory_make("audioresample", NULL);
		//audioenc = gst_element_factory_make("speexenc", NULL);
		audioenc = gst_element_factory_make("speexenc", NULL);
		//audiortppay = gst_element_factory_make("rtpspeexpay", NULL);
		audiortppay = gst_element_factory_make("rtpspeexpay", NULL);
		//g_object_set(G_OBJECT(audiortppay), "min-ptime", (guint64)1000000000, NULL);
		//audiosink = gst_element_factory_make("alsasink", NULL);
		//audiortpsink = gst_element_factory_make("fakesink", NULL);
		//g_object_set(G_OBJECT(audiortpsink), "sync", TRUE, NULL);
		audiortpsink = gst_element_factory_make("apprtpsink", NULL);
		appRtpSink = (GstAppRtpSink *)audiortpsink;
		appRtpSink->appdata = this;
		appRtpSink->packet_ready = cb_packet_ready_rtp_audio;
		g_object_set(G_OBJECT(appRtpSink), "sync", TRUE, NULL);

		caps5 = gst_caps_new_simple("audio/x-raw-int",
			"rate", G_TYPE_INT, 16000,
			"channels", G_TYPE_INT, 1, NULL);
		/*caps5 = gst_caps_new_simple("audio/x-raw-float",
			"rate", G_TYPE_INT, 16000,
			"channels", G_TYPE_INT, 1, NULL);*/
	}

	GstElement *videoqueue = 0, *videoconvert = 0, *videosink = 0;
	GstAppVideoSink *appVideoSink = 0;
	GstElement *videoconvertpre = 0, *videotee = 0, *videortpqueue = 0, *videoenc = 0, *videortppay = 0, *videortpsink = 0;

	if(videoin || fileSource)
	{
		videoqueue = gst_element_factory_make("queue", NULL);
		videoconvert = gst_element_factory_make("ffmpegcolorspace", NULL);
		//videosink = gst_element_factory_make("ximagesink", NULL);
		videosink = gst_element_factory_make("appvideosink", NULL);
		if(!videosink)
		{
			printf("could not make videosink!!\n");
		}
		appVideoSink = (GstAppVideoSink *)videosink;
		appVideoSink->appdata = this;
		appVideoSink->show_frame = cb_show_frame_preview;
		//g_object_set(G_OBJECT(appVideoSink), "sync", FALSE, NULL);

		videoconvertpre = gst_element_factory_make("ffmpegcolorspace", NULL);
		videotee = gst_element_factory_make("tee", NULL);
		videortpqueue = gst_element_factory_make("queue", NULL);
		videoenc = gst_element_factory_make("theoraenc", NULL);
		videortppay = gst_element_factory_make("rtptheorapay", NULL);
		videortpsink = gst_element_factory_make("apprtpsink", NULL);
		//videortpsink = gst_element_factory_make("fakesink", NULL);
		//g_object_set(G_OBJECT(videortpsink), "sync", TRUE, NULL);
		if(!videotee || !videortpqueue || !videoenc || !videortppay || !videortpsink)
			printf("error making some video stuff\n");
		appRtpSink = (GstAppRtpSink *)videortpsink;
		appRtpSink->appdata = this;
		appRtpSink->packet_ready = cb_packet_ready_rtp_video;
		g_object_set(G_OBJECT(appRtpSink), "sync", TRUE, NULL);
	}

	if(audioin)
		gst_bin_add(GST_BIN(pipeline), audioin);
	if(videoin)
		gst_bin_add(GST_BIN(pipeline), videoin);

	if(fileSource)
		gst_bin_add_many(GST_BIN(pipeline), fileSource, fileDemux, NULL);

	if(audioin || fileSource)
		gst_bin_add_many(GST_BIN(pipeline), audioqueue, audioconvert, audioresample, audioenc, audiortppay, audiortpsink, NULL);
	if(videoin || fileSource)
	{
		gst_bin_add_many(GST_BIN(pipeline), videoconvertpre, videotee, videoqueue, videoconvert, videosink, NULL);
		gst_bin_add_many(GST_BIN(pipeline), videortpqueue, videoenc, videortppay, videortpsink, NULL);
	}

	if(fileSource)
		gst_element_link_many(fileSource, fileDemux, NULL);

	if(audioin || fileSource)
	{
		gst_element_link_many(audioqueue, audioconvert, audioresample, NULL);
		gst_element_link_filtered(audioresample, audioenc, caps5);
		//gst_element_link_many(audioqueue, audioresample, NULL);
		//gst_element_link_filtered(audioresample, audioconvert, caps5);
		//gst_element_link_many(audioconvert, audioenc, NULL);

		gst_element_link_many(audioenc, audiortppay, audiortpsink, NULL);
	}
	if(videoin || fileSource)
	{
		gst_element_link_many(videoconvertpre, videotee, videoqueue, videoconvert, videosink, NULL);
		gst_element_link_many(videotee, videortpqueue, videoenc, videortppay, videortpsink, NULL);
	}

	if(audioin || fileSource)
		audioTarget = audioqueue;
	if(videoin || fileSource)
		videoTarget = videoconvertpre;

	if(audioin)
		gst_element_link_many(audioin, audioTarget, NULL);
	if(videoin)
	{
		if(videoincaps)
			gst_element_link_filtered(videoin, videoTarget, videoincaps);
		else
			gst_element_link(videoin, videoTarget);
	}

	//GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(e_pipeline));
	//gst_bus_add_watch(bus, bus_call, loop);
	//gst_object_unref(bus);

	// ### seems for live streams we need playing state to get caps.
	//   paused may not be enough??
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

	if(audioin || fileSource)
	{
		GstPad *pad = gst_element_get_pad(audiortppay, "src");
		GstCaps *caps = gst_pad_get_negotiated_caps(pad);
		gchar *gstr = gst_caps_to_string(caps);
		QString capsString = QString::fromUtf8(gstr);
		g_free(gstr);
		printf("rtppay caps audio: [%s]\n", qPrintable(capsString));
		g_object_unref(pad);

		GstStructure *cs = gst_caps_get_structure(caps, 0);

		localAudioPayloadInfo = structureToPayloadInfo(cs);
		if(localAudioPayloadInfo.id == -1)
		{
			// TODO: handle error
		}

		gst_caps_unref(caps);
	}

	if(videoin || fileSource)
	{
		GstPad *pad = gst_element_get_pad(videortppay, "src");
		GstCaps *caps = gst_pad_get_negotiated_caps(pad);
		gchar *gstr = gst_caps_to_string(caps);
		QString capsString = QString::fromUtf8(gstr);
		g_free(gstr);
		printf("rtppay caps video: [%s]\n", qPrintable(capsString));
		gst_object_unref(pad);

		GstStructure *cs = gst_caps_get_structure(caps, 0);

		localVideoPayloadInfo = structureToPayloadInfo(cs);
		if(localVideoPayloadInfo.id == -1)
		{
			// TODO: handle error
		}

		gst_caps_unref(caps);
	}

	//gst_element_set_state(pipeline, GST_STATE_PLAYING);
	//gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

	if(cb_started)
		cb_started(app);

	}

	return FALSE;

dorecv:
	{

	rpipeline = gst_pipeline_new(NULL);
	rvpipeline = gst_pipeline_new(NULL);

#ifdef UDP_LOOPBACK
	audiortpsrc = gst_element_factory_make("udpsrc", NULL);
	g_object_set(G_OBJECT(audiortpsrc), "port", 61000, NULL);
#else
	audiortpsrc = gst_element_factory_make("apprtpsrc", NULL);
#endif

	//GstStructure *cs = gst_structure_from_string("application/x-rtp, media=(string)audio, clock-rate=(int)16000, encoding-name=(string)SPEEX, encoding-params=(string)1, payload=(int)110", NULL);
	//GstStructure *cs = gst_structure_from_string("application/x-rtp, media=(string)audio, clock-rate=(int)8000, encoding-name=(string)PCMU, payload=(int)0", NULL);
	GstStructure *cs = payloadInfoToStructure(remoteAudioPayloadInfo, "audio");
	if(!cs)
	{
		// TODO: handle error
		printf("cannot parse payload info\n");
	}

	GstCaps *caps = gst_caps_new_empty();
	gst_caps_append_structure(caps, cs);
	g_object_set(G_OBJECT(audiortpsrc), "caps", caps, NULL);
	gst_caps_unref(caps);

#ifdef UDP_LOOPBACK
	videortpsrc = gst_element_factory_make("udpsrc", NULL);
	g_object_set(G_OBJECT(videortpsrc), "port", 61002, NULL);
#else
	videortpsrc = gst_element_factory_make("apprtpsrc", NULL);
#endif
	cs = payloadInfoToStructure(remoteVideoPayloadInfo, "video");
	if(!cs)
	{
		// TODO: handle error
		printf("cannot parse payload info\n");
	}

	caps = gst_caps_new_empty();
	gst_caps_append_structure(caps, cs);
	g_object_set(G_OBJECT(videortpsrc), "caps", caps, NULL);
	gst_caps_unref(caps);

	//GstElement *audioqueue = gst_element_factory_make("queue", NULL);
	GstElement *audiortpjitterbuffer = gst_element_factory_make("gstrtpjitterbuffer", NULL);
	//GstElement *audiortpdepay = gst_element_factory_make("rtpspeexdepay", NULL);
	GstElement *audiortpdepay = gst_element_factory_make("rtpspeexdepay", NULL);
	//GstElement *audiodec = gst_element_factory_make("speexdec", NULL);
	GstElement *audiodec = gst_element_factory_make("speexdec", NULL);
	GstElement *audioconvert = gst_element_factory_make("audioconvert", NULL);
	GstElement *audioresample = gst_element_factory_make("audioresample", NULL);
	GstElement *audioout = 0;

	if(audiortpjitterbuffer)
	{
		gst_bin_add_many(GST_BIN(rpipeline), audiortpsrc, audiortpjitterbuffer, audiortpdepay, audiodec, audioconvert, audioresample, NULL);
		gst_element_link_many(audiortpsrc, audiortpjitterbuffer, audiortpdepay, audiodec, audioconvert, audioresample, NULL);
		g_object_set(G_OBJECT(audiortpjitterbuffer), "latency", (unsigned int)400, NULL);
	}
	else
	{
		gst_bin_add_many(GST_BIN(rpipeline), audiortpsrc, audiortpdepay, audiodec, audioconvert, audioresample, NULL);
		gst_element_link_many(audiortpsrc, audiortpdepay, audiodec, audioconvert, audioresample, NULL);
	}

	if(!aout.isEmpty())
	{
		printf("creating audioout\n");

		audioout = make_device_element(aout, PDevice::AudioOut);
		if(!audioout)
		{
			// TODO
			printf("failed to create audio output element\n");
		}
	}
	else
		audioout = gst_element_factory_make("fakesink", NULL);

	//GstElement *videoqueue = gst_element_factory_make("queue", NULL);
	GstElement *videortpjitterbuffer = gst_element_factory_make("gstrtpjitterbuffer", NULL);
	GstElement *videortpdepay = gst_element_factory_make("rtptheoradepay", NULL);
	GstElement *videodec = gst_element_factory_make("theoradec", NULL);
	GstElement *videoconvert = gst_element_factory_make("ffmpegcolorspace", NULL);
	//GstElement *videosink = gst_element_factory_make("ximagesink", NULL);
	GstElement *videosink = gst_element_factory_make("appvideosink", NULL);
	if(!videosink)
	{
		printf("could not make videosink!!\n");
	}
	GstAppVideoSink *appVideoSink = (GstAppVideoSink *)videosink;
	appVideoSink->appdata = this;
	appVideoSink->show_frame = cb_show_frame_output;

	if(videortpjitterbuffer)
	{
		gst_bin_add_many(GST_BIN(rvpipeline), videortpsrc, /*videoqueue,*/ videortpjitterbuffer, videortpdepay, videodec, videoconvert, videosink, NULL);
		gst_element_link_many(videortpsrc, /*videoqueue,*/ videortpjitterbuffer, videortpdepay, videodec, videoconvert, videosink, NULL);
		g_object_set(G_OBJECT(audiortpjitterbuffer), "latency", (unsigned int)400, NULL);
	}
	else
	{
		gst_bin_add_many(GST_BIN(rvpipeline), videortpsrc, /*videoqueue,*/ videortpdepay, videodec, videoconvert, videosink, NULL);
		gst_element_link_many(videortpsrc, /*videoqueue,*/ videortpdepay, videodec, videoconvert, videosink, NULL);
	}

	gst_bin_add(GST_BIN(rpipeline), audioout);
	gst_element_link_many(audioresample, audioout, NULL);

	gst_element_set_state(rpipeline, GST_STATE_READY);
	gst_element_get_state(rpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

	gst_element_set_state(rvpipeline, GST_STATE_READY);
	gst_element_get_state(rvpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

	gst_element_set_state(rpipeline, GST_STATE_PLAYING);

	gst_element_set_state(rvpipeline, GST_STATE_PLAYING);

	printf("receive pipeline started\n");

	if(cb_started)
		cb_started(app);

	}

	return FALSE;
}

gboolean RtpWorker::doStop()
{
	cleanup();

	if(cb_stopped)
		cb_stopped(app);

	return FALSE;
}

void RtpWorker::fileDemux_no_more_pads(GstElement *element)
{
	Q_UNUSED(element);
	printf("no more pads\n");
}

void RtpWorker::fileDemux_pad_added(GstElement *element, GstPad *pad)
{
	Q_UNUSED(element);

	gchar *name = gst_pad_get_name(pad);
	printf("pad-added: %s\n", name);
	g_free(name);

	GstCaps *caps = gst_pad_get_caps(pad);
	gchar *gstr = gst_caps_to_string(caps);
	QString capsString = QString::fromUtf8(gstr);
	g_free(gstr);
	printf("  caps: [%s]\n", qPrintable(capsString));

	int num = gst_caps_get_size(caps);
	for(int n = 0; n < num; ++n)
	{
		GstStructure *cs = gst_caps_get_structure(caps, n);
		QString mime = gst_structure_get_name(cs);

		QStringList parts = mime.split('/');
		if(parts.count() != 2)
			continue;
		QString type = parts[0];
		QString subtype = parts[1];

		GstElement *decoder = 0;
		GstElement *target = 0;

		// FIXME: in the future, we should probably do this
		//   more dynamically, by inspecting the pads on the
		//   decoder and comparing to the source pad, rather
		//   than assuming fixed values (like 'x-speex').
		if(type == "audio")
		{
			target = audioTarget;

			if(subtype == "x-speex")
				decoder = gst_element_factory_make("speexdec", NULL);
			else if(subtype == "x-vorbis")
				decoder = gst_element_factory_make("vorbisdec", NULL);
		}
		else if(type == "video")
		{
			target = videoTarget;

			if(subtype == "x-theora")
				decoder = gst_element_factory_make("theoradec", NULL);
		}

		if(decoder)
		{
			if(!gst_bin_add(GST_BIN(pipeline), decoder))
				continue;
			GstPad *sinkpad = gst_element_get_static_pad(decoder, "sink");
			if(!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(pad, sinkpad)))
				continue;
			gst_object_unref(sinkpad);

			GstPad *sourcepad = gst_element_get_static_pad(decoder, "src");
			sinkpad = gst_element_get_static_pad(target, "sink");
			if(!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(sourcepad, sinkpad)))
				continue;
			gst_object_unref(sourcepad);
			gst_object_unref(sinkpad);

			// by default the element is not in a working state.
			//   we set to 'paused' which hopefully means it'll
			//   do the right thing.
			gst_element_set_state(decoder, GST_STATE_PAUSED);

			// decoder set up, we're done
			break;
		}
	}

	gst_caps_unref(caps);
}

void RtpWorker::fileDemux_pad_removed(GstElement *element, GstPad *pad)
{
	Q_UNUSED(element);

	// TODO

	gchar *name = gst_pad_get_name(pad);
	printf("pad-removed: %s\n", name);
	g_free(name);
}

gboolean RtpWorker::bus_call(GstBus *bus, GstMessage *msg)
{
	Q_UNUSED(bus);
	Q_UNUSED(msg);
	return TRUE;

	/*Q_UNUSED(bus);
	GMainLoop *loop = (GMainLoop *)data;
	switch(GST_MESSAGE_TYPE(msg))
	{
		case GST_MESSAGE_EOS:
		{
			g_print("End-of-stream\n");
			g_main_loop_quit(loop);
			break;
		}
		case GST_MESSAGE_ERROR:
		{
			gchar *debug;
			GError *err;

			gst_message_parse_error(msg, &err, &debug);
			g_free(debug);

			g_print("Error: %s\n", err->message);
			g_error_free(err);

			g_main_loop_quit(loop);
			break;
		}
		default:
			break;
	}

	return TRUE;*/
}

void RtpWorker::show_frame_preview(int width, int height, const unsigned char *rgb24)
{
	QImage image(width, height, QImage::Format_RGB32);
	int at = 0;
	for(int y = 0; y < height; ++y)
	{
		for(int x = 0; x < width; ++x)
		{
			unsigned char r = rgb24[at++];
			unsigned char g = rgb24[at++];
			unsigned char b = rgb24[at++];
			QRgb color = qRgb(r, g, b);
			image.setPixel(x, y, color);
		}
	}

	Frame frame;
	frame.image = image;

	if(cb_previewFrame)
		cb_previewFrame(frame, app);
}

void RtpWorker::show_frame_output(int width, int height, const unsigned char *rgb24)
{
	QImage image(width, height, QImage::Format_RGB32);
	int at = 0;
	for(int y = 0; y < height; ++y)
	{
		for(int x = 0; x < width; ++x)
		{
			unsigned char r = rgb24[at++];
			unsigned char g = rgb24[at++];
			unsigned char b = rgb24[at++];
			QRgb color = qRgb(r, g, b);
			image.setPixel(x, y, color);
		}
	}

	Frame frame;
	frame.image = image;

	if(cb_outputFrame)
		cb_outputFrame(frame, app);
}

// stats gathering
static int acalls = 0;
static int asizes[30];
static int asizes_at = 0;
static QTime acalltime;
static int vcalls = 0;
static int vsizes[30];
static int vsizes_at = 0;
static QTime vcalltime;

void RtpWorker::packet_ready_rtp_audio(const unsigned char *buf, int size)
{
	QByteArray ba((const char *)buf, size);
	PRtpPacket packet;
	packet.rawValue = ba;
	packet.portOffset = 0;

	if(asizes_at >= 30)
	{
		memmove(asizes, asizes + 1, sizeof(int) * (asizes_at - 1));
		--asizes_at;
	}
	asizes[asizes_at++] = packet.rawValue.size();

	if(acalls == 0)
		acalltime.start();
	if(acalls == 500)
	{
		int avg = 0;
		for(int n = 0; n < asizes_at; ++n)
			avg += asizes[n];
		avg /= asizes_at;
		printf("audio: time.elapsed=%d, avg=%d\n", acalltime.elapsed(), avg);
	}
	++acalls;

	if(cb_rtpAudioOut)
		cb_rtpAudioOut(packet, app);
}

void RtpWorker::packet_ready_rtp_video(const unsigned char *buf, int size)
{
	QByteArray ba((const char *)buf, size);
	PRtpPacket packet;
	packet.rawValue = ba;
	packet.portOffset = 0;

	if(vsizes_at >= 30)
	{
		memmove(vsizes, vsizes + 1, sizeof(int) * (vsizes_at - 1));
		--vsizes_at;
	}
	vsizes[vsizes_at++] = packet.rawValue.size();

	if(vcalls == 0)
		vcalltime.start();
	if(vcalls == 500)
	{
		int avg = 0;
		for(int n = 0; n < vsizes_at; ++n)
			avg += vsizes[n];
		avg /= vsizes_at;
		printf("video: time.elapsed=%d, avg=%d\n", vcalltime.elapsed(), avg);
	}
	++vcalls;

	if(cb_rtpVideoOut)
		cb_rtpVideoOut(packet, app);
}

void RtpWorker::cleanup()
{
	if(pipeline)
	{
		gst_element_set_state(pipeline, GST_STATE_NULL);
		gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

		gst_object_unref(GST_OBJECT(pipeline));
		pipeline = 0;
	}

	if(rpipeline)
	{
		gst_element_set_state(rpipeline, GST_STATE_NULL);
		gst_element_get_state(rpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

		gst_object_unref(GST_OBJECT(rpipeline));
		rpipeline = 0;
	}

	if(rvpipeline)
	{
		gst_element_set_state(rvpipeline, GST_STATE_NULL);
		gst_element_get_state(rvpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

		gst_object_unref(GST_OBJECT(rvpipeline));
		rvpipeline = 0;
	}
}

}
