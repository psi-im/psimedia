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
#include <QTime>
#include "devices.h"
#include "payloadinfo.h"

// TODO: support playing from bytearray
// TODO: support recording
// FIXME: we probably shouldn't be looking up pads by name?

namespace PsiMedia {

static GstElement *audio_codec_to_enc_element(const QString &name)
{
	QString ename;
	if(name == "speex")
		ename = "speexenc";
	else if(name == "vorbis")
		ename = "vorbisenc";
	else if(name == "pcmu")
		ename = "mulawenc";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static GstElement *audio_codec_to_dec_element(const QString &name)
{
	QString ename;
	if(name == "speex")
		ename = "speexdec";
	else if(name == "vorbis")
		ename = "vorbisdec";
	else if(name == "pcmu")
		ename = "mulawdec";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static GstElement *audio_codec_to_rtppay_element(const QString &name)
{
	QString ename;
	if(name == "speex")
		ename = "rtpspeexpay";
	else if(name == "vorbis")
		ename = "rtpvorbispay";
	else if(name == "pcmu")
		ename = "rtppcmupay";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static GstElement *audio_codec_to_rtpdepay_element(const QString &name)
{
	QString ename;
	if(name == "speex")
		ename = "rtpspeexdepay";
	else if(name == "vorbis")
		ename = "rtpvorbisdepay";
	else if(name == "pcmu")
		ename = "rtppcmudepay";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static GstElement *video_codec_to_enc_element(const QString &name)
{
	QString ename;
	if(name == "theora")
		ename = "theoraenc";
	else if(name == "h263p")
		ename = "ffenc_h263p";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static GstElement *video_codec_to_dec_element(const QString &name)
{
	QString ename;
	if(name == "theora")
		ename = "theoradec";
	else if(name == "h263p")
		ename = "ffdec_h263";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static GstElement *video_codec_to_rtppay_element(const QString &name)
{
	QString ename;
	if(name == "theora")
		ename = "rtptheorapay";
	else if(name == "h263p")
		ename = "rtph263ppay";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static GstElement *video_codec_to_rtpdepay_element(const QString &name)
{
	QString ename;
	if(name == "theora")
		ename = "rtptheoradepay";
	else if(name == "h263p")
		ename = "rtph263pdepay";
	else
		return 0;

	return gst_element_factory_make(ename.toLatin1().data(), NULL);
}

static bool audio_codec_get_send_elements(const QString &name, GstElement **enc, GstElement **rtppay)
{
	GstElement *eenc = audio_codec_to_enc_element(name);
	if(!eenc)
		return false;
	GstElement *epay = audio_codec_to_rtppay_element(name);
	if(!epay)
	{
		g_object_unref(G_OBJECT(eenc));
	}

	*enc = eenc;
	*rtppay = epay;
	return true;
}

static bool audio_codec_get_recv_elements(const QString &name, GstElement **dec, GstElement **rtpdepay)
{
	GstElement *edec = audio_codec_to_dec_element(name);
	if(!edec)
		return false;
	GstElement *edepay = audio_codec_to_rtpdepay_element(name);
	if(!edepay)
	{
		g_object_unref(G_OBJECT(edec));
	}

	*dec = edec;
	*rtpdepay = edepay;
	return true;
}

static bool video_codec_get_send_elements(const QString &name, GstElement **enc, GstElement **rtppay)
{
	GstElement *eenc = video_codec_to_enc_element(name);
	if(!eenc)
		return false;
	GstElement *epay = video_codec_to_rtppay_element(name);
	if(!epay)
	{
		g_object_unref(G_OBJECT(eenc));
	}

	*enc = eenc;
	*rtppay = epay;
	return true;
}

static bool video_codec_get_recv_elements(const QString &name, GstElement **dec, GstElement **rtpdepay)
{
	GstElement *edec = video_codec_to_dec_element(name);
	if(!edec)
		return false;
	GstElement *edepay = video_codec_to_rtpdepay_element(name);
	if(!edepay)
	{
		g_object_unref(G_OBJECT(edec));
	}

	*dec = edec;
	*rtpdepay = edepay;
	return true;
}

// adapted from psimedia.cpp
static bool compare_PPayloadInfo_Parameter(const PPayloadInfo::Parameter &a, const PPayloadInfo::Parameter &b)
{
	// according to xep-167, parameter names are case-sensitive
	if(a.name == b.name && a.value == b.value)
		return true;
	else
		return false;
}

static bool compareUnordered_PPayloadInfo_Parameter(const QList<PPayloadInfo::Parameter> &a, const QList<PPayloadInfo::Parameter> &b)
{
	if(a.count() != b.count())
		return false;

	// for every parameter in 'a'
	foreach(const PPayloadInfo::Parameter &p, a)
	{
		// make sure it is found in 'b'
		bool found = false;
		foreach(const PPayloadInfo::Parameter &p2, b)
		{
			if(compare_PPayloadInfo_Parameter(p2, p))
			{
				found = true;
				break;
			}
		}
		if(!found)
			return false;
	}

	return true;
}

static bool compare_PPayloadInfo(const PPayloadInfo &a, const PPayloadInfo &b)
{
	// according to xep-167, parameters are unordered
	if(a.id == b.id &&
		a.name == b.name &&
		a.clockrate == b.clockrate &&
		a.channels == b.channels &&
		a.ptime == b.ptime &&
		a.maxptime == b.maxptime &&
		compareUnordered_PPayloadInfo_Parameter(a.parameters, b.parameters))
	{
		return true;
	}
	else
		return false;

	return true;
}

class VideoCaps
{
public:
	QSize size;
	QList<double> framerates;
};

//----------------------------------------------------------------------------
// GstBusSource
//----------------------------------------------------------------------------
// FIXME: remove this inefficient code in the future.
// See http://bugzilla.gnome.org/show_bug.cgi?id=562170
typedef struct
{
	GSource source;
	GstBus *bus;
} GstBusSource;

static gboolean source_prepare(GSource *source, gint *timeout)
{
	GstBusSource *bsrc = (GstBusSource *)source;

	*timeout = 0;
	return gst_bus_have_pending(bsrc->bus);
}

static gboolean source_check(GSource *source)
{
	GstBusSource *bsrc = (GstBusSource *)source;
	return gst_bus_have_pending(bsrc->bus);
}

static gboolean source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
	GstBusFunc handler = (GstBusFunc)callback;
	GstBusSource *bsrc = (GstBusSource *)source;
	gboolean result = FALSE;

	if (handler)
	{
		GstMessage *message = gst_bus_pop(bsrc->bus);
		if(message)
		{
			result = handler(bsrc->bus, message, user_data);
			gst_message_unref(message);
		}
	}

	return result;
}

static void source_finalize(GSource *source)
{
	GstBusSource *bsrc = (GstBusSource *)source;
	gst_object_unref(bsrc->bus);
	bsrc->bus = NULL;
}

Q_GLOBAL_STATIC(QMutex, source_funcs_mutex)
static GSourceFuncs source_funcs;
static bool set_funcs = false;

static void source_ensureInit()
{
	source_funcs_mutex()->lock();
	if(!set_funcs)
	{
		memset(&source_funcs, 0, sizeof(GSourceFuncs));
		source_funcs.prepare = source_prepare;
		source_funcs.check = source_check;
		source_funcs.dispatch = source_dispatch;
		source_funcs.finalize = source_finalize;
		set_funcs = true;
	}
	source_funcs_mutex()->unlock();
}

//----------------------------------------------------------------------------
// RtpWorker
//----------------------------------------------------------------------------
RtpWorker::RtpWorker(GMainContext *mainContext) :
	loopFile(false),
	canTransmitAudio(false),
	canTransmitVideo(false),
	outputVolume(100),
	inputVolume(100),
	cb_started(0),
	cb_updated(0),
	cb_stopped(0),
	cb_finished(0),
	cb_error(0),
	cb_audioIntensity(0),
	cb_previewFrame(0),
	cb_outputFrame(0),
	cb_rtpAudioOut(0),
	cb_rtpVideoOut(0),
	mainContext_(mainContext),
	timer(0),
	sendPipeline(0),
	rpipeline(0),
	rvpipeline(0),
	audiortpsrc(0),
	videortpsrc(0),
	volumein(0),
	volumeout(0),
	rtpaudioout(false),
	rtpvideoout(false),
	recordTimer(0)
{
	source_ensureInit();
}

RtpWorker::~RtpWorker()
{
	if(timer)
	{
		g_source_destroy(timer);
		timer = 0;
	}

	if(recordTimer)
	{
		g_source_destroy(recordTimer);
		recordTimer = 0;
	}

	cleanup();
}

void RtpWorker::cleanup()
{
	printf("cleaning up...\n");
	volumein_mutex.lock();
	volumein = 0;
	volumein_mutex.unlock();

	volumeout_mutex.lock();
	volumeout = 0;
	volumeout_mutex.unlock();

	audiortpsrc_mutex.lock();
	audiortpsrc = 0;
	audiortpsrc_mutex.unlock();

	videortpsrc_mutex.lock();
	videortpsrc = 0;
	videortpsrc_mutex.unlock();

	rtpaudioout_mutex.lock();
	rtpaudioout = false;
	rtpaudioout_mutex.unlock();

	rtpvideoout_mutex.lock();
	rtpvideoout = false;
	rtpvideoout_mutex.unlock();

	if(sendPipeline)
	{
		gst_element_set_state(sendPipeline, GST_STATE_NULL);
		gst_element_get_state(sendPipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

		gst_object_unref(GST_OBJECT(sendPipeline));
		sendPipeline = 0;
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
	printf("cleaning done.\n");
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
	Q_ASSERT(!timer);
	timer = g_timeout_source_new(0);
	g_source_set_callback(timer, cb_doUpdate, this, NULL);
	g_source_attach(timer, mainContext_);
}

void RtpWorker::transmitAudio()
{
	QMutexLocker locker(&rtpaudioout_mutex);
	rtpaudioout = true;
}

void RtpWorker::transmitVideo()
{
	QMutexLocker locker(&rtpvideoout_mutex);
	rtpvideoout = true;
}

void RtpWorker::pauseAudio()
{
	QMutexLocker locker(&rtpaudioout_mutex);
	rtpaudioout = false;
}

void RtpWorker::pauseVideo()
{
	QMutexLocker locker(&rtpvideoout_mutex);
	rtpvideoout = false;
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
	QMutexLocker locker(&audiortpsrc_mutex);
	if(packet.portOffset == 0 && audiortpsrc)
		gst_apprtpsrc_packet_push((GstAppRtpSrc *)audiortpsrc, (const unsigned char *)packet.rawValue.data(), packet.rawValue.size());
}

void RtpWorker::rtpVideoIn(const PRtpPacket &packet)
{
	QMutexLocker locker(&videortpsrc_mutex);
	if(packet.portOffset == 0 && videortpsrc)
		gst_apprtpsrc_packet_push((GstAppRtpSrc *)videortpsrc, (const unsigned char *)packet.rawValue.data(), packet.rawValue.size());
}

void RtpWorker::setOutputVolume(int level)
{
	QMutexLocker locker(&volumeout_mutex);
	outputVolume = level;
	if(volumeout)
	{
		double vol = (double)level / 100;
		g_object_set(G_OBJECT(volumeout), "volume", vol, NULL);
	}
}

void RtpWorker::setInputVolume(int level)
{
	QMutexLocker locker(&volumein_mutex);
	inputVolume = level;
	if(volumein)
	{
		double vol = (double)level / 100;
		g_object_set(G_OBJECT(volumein), "volume", vol, NULL);
	}
}

void RtpWorker::recordStart()
{
	// FIXME: for now we just send EOF/error
	if(cb_recordData)
		cb_recordData(QByteArray(), app);
}

void RtpWorker::recordStop()
{
	// TODO: assert recording
	// FIXME: don't just do nothing
}

gboolean RtpWorker::cb_doStart(gpointer data)
{
	return ((RtpWorker *)data)->doStart();
}

gboolean RtpWorker::cb_doUpdate(gpointer data)
{
	return ((RtpWorker *)data)->doUpdate();
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

void RtpWorker::cb_videoDecode_pad_added(GstElement *element, GstPad *pad, gpointer data)
{
	((RtpWorker *)data)->videoDecode_pad_added(element, pad);
}

gboolean RtpWorker::cb_bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
	return ((RtpWorker *)data)->bus_call(bus, msg);
}

void RtpWorker::cb_show_frame_preview(int width, int height, const unsigned char *rgb32, gpointer data)
{
	((RtpWorker *)data)->show_frame_preview(width, height, rgb32);
}

void RtpWorker::cb_show_frame_output(int width, int height, const unsigned char *rgb32, gpointer data)
{
	((RtpWorker *)data)->show_frame_output(width, height, rgb32);
}

void RtpWorker::cb_packet_ready_rtp_audio(const unsigned char *buf, int size, gpointer data)
{
	((RtpWorker *)data)->packet_ready_rtp_audio(buf, size);
}

void RtpWorker::cb_packet_ready_rtp_video(const unsigned char *buf, int size, gpointer data)
{
	((RtpWorker *)data)->packet_ready_rtp_video(buf, size);
}

gboolean RtpWorker::cb_fileReady(gpointer data)
{
	return ((RtpWorker *)data)->fileReady();
}

gboolean RtpWorker::doStart()
{
	timer = 0;

	audiosrc = 0;
	videosrc = 0;
	audiortpsrc = 0;
	videortpsrc = 0;
	audiortppay = 0;
	videortppay = 0;

	desired_speex_pt = -1;
	printf("remoteAudioPayloadInfo count: %d\n", remoteAudioPayloadInfo.count());
	printf("localAudioParams count: %d\n", localAudioParams.count());
	for(int n = 0; n < remoteAudioPayloadInfo.count(); ++n)
	{
		const PPayloadInfo &ri = remoteAudioPayloadInfo[n];

		printf("  remote: %d,%s,%d\n", ri.id, qPrintable(ri.name), ri.clockrate);
		for(int i = 0; i < localAudioParams.count(); ++i)
		{
			const PAudioParams &lp = localAudioParams[i];

			printf("  local: %s,%d\n", qPrintable(lp.codec), lp.sampleRate);
			if(lp.codec == "speex" && ri.name == "SPEEX" && lp.sampleRate == ri.clockrate)
			{
				desired_speex_pt = ri.id;
				break;
			}
		}

		if(desired_speex_pt != -1)
			break;
	}

	// file source
	if(!infile.isEmpty() || !indata.isEmpty())
	{
		sendPipeline = gst_pipeline_new(NULL);

		GstElement *fileSource = gst_element_factory_make("filesrc", NULL);
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

		gst_bin_add(GST_BIN(sendPipeline), fileSource);
		gst_bin_add(GST_BIN(sendPipeline), fileDemux);
		gst_element_link(fileSource, fileDemux);
	}
	// device source
	else if(!ain.isEmpty() || !vin.isEmpty())
	{
		sendPipeline = gst_pipeline_new(NULL);

		if(!ain.isEmpty())
		{
			GstElement *audioin = devices_makeElement(ain, PDevice::AudioIn);
			if(!audioin)
			{
				printf("Failed to create audio input element '%s'.\n", qPrintable(ain));
				error = RtpSessionContext::ErrorGeneric;
				if(cb_error)
					cb_error(app);
				return FALSE;
			}
			gst_bin_add(GST_BIN(sendPipeline), audioin);
			audiosrc = audioin;
		}

		if(!vin.isEmpty())
		{
			QSize captureSize;
			GstElement *videoin = devices_makeElement(vin, PDevice::VideoIn, &captureSize);
			if(!videoin)
			{
				printf("Failed to create video input element '%s'.\n", qPrintable(vin));
				error = RtpSessionContext::ErrorGeneric;
				if(cb_error)
					cb_error(app);
				return FALSE;
			}

			if(localVideoParams.isEmpty())
			{
				g_object_unref(G_OBJECT(videoin));
				error = RtpSessionContext::ErrorGeneric;
				if(cb_error)
					cb_error(app);
				return FALSE;
			}

			QSize sendSize = localVideoParams[0].size;

			// obtain video modes
			gst_element_set_state(videoin, GST_STATE_PAUSED);
			gst_element_get_state(videoin, NULL, NULL, GST_CLOCK_TIME_NONE);

			QList<VideoCaps> modes;
			GstPad *pad = gst_element_get_static_pad(videoin, "src");
			GstCaps *caps = gst_pad_get_caps(pad);
			int num = gst_caps_get_size(caps);
			QStringList mediaTypes;
			mediaTypes << "video/x-raw-yuv" << "video/x-raw-rgb";
			for(int n = 0; n < num; ++n)
			{
				VideoCaps m;
				GstStructure *cs = gst_caps_get_structure(caps, n);
				const gchar *name = gst_structure_get_name(cs);
				if(!name)
					continue;
				if(!mediaTypes.contains(QString(name)))
					continue;
				//printf("cs=[%s]\n", gst_structure_to_string(cs));
				gint w, h, fn, fd;
				if(!gst_structure_get_int(cs, "width", &w))
					continue;
				if(!gst_structure_get_int(cs, "height", &h))
					continue;
				const GValue *val = gst_structure_get_value(cs, "framerate");
				if(!val)
					continue;
				if(GST_VALUE_HOLDS_LIST(val))
				{
					for(int i = 0; i < (int)gst_value_list_get_size(val); ++i)
					{
						const GValue *sval = gst_value_list_get_value(val, i);
						if(GST_VALUE_HOLDS_FRACTION(sval))
						{
							fn = gst_value_get_fraction_numerator(sval);
							fd = gst_value_get_fraction_denominator(sval);

							m.framerates += (double)fn / (double)fd;
						}
					}
				}
				else if(GST_VALUE_HOLDS_FRACTION(val))
				{
					if(!gst_structure_get_fraction(cs, "framerate", &fn, &fd))
						continue;

					m.framerates += (double)fn / (double)fd;
				}
				if(w < 1 || h < 1)
					continue;

				m.size = QSize(w, h);
				modes += m;
			}
			gst_caps_unref(caps);
			gst_object_unref(pad);

			printf("capture modes:\n");
			foreach(const VideoCaps &m, modes)
			{
				foreach(const double &rate, m.framerates)
					printf("  %dx%d @ %dfps\n", m.size.width(), m.size.height(), (int)rate);
			}

			gst_bin_add(GST_BIN(sendPipeline), videoin);

			if(captureSize.isValid())
			{
				GstElement *capsfilter = gst_element_factory_make("capsfilter", NULL);
				GstCaps *caps = gst_caps_new_empty();

				GstStructure *cs;
				cs = gst_structure_new("video/x-raw-yuv",
					"width", G_TYPE_INT, captureSize.width(),
					"height", G_TYPE_INT, captureSize.height(), NULL);
				gst_caps_append_structure(caps, cs);

				cs = gst_structure_new("video/x-raw-rgb",
					"width", G_TYPE_INT, captureSize.width(),
					"height", G_TYPE_INT, captureSize.height(), NULL);
				gst_caps_append_structure(caps, cs);

				cs = gst_structure_new("image/jpeg",
					"width", G_TYPE_INT, captureSize.width(),
					"height", G_TYPE_INT, captureSize.height(), NULL);
				gst_caps_append_structure(caps, cs);

				g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
				gst_caps_unref(caps);

				gst_bin_add(GST_BIN(sendPipeline), capsfilter);
				gst_element_link(videoin, capsfilter);

				videosrc = capsfilter;
			}
			else
			{
				QList<int> widths;
				widths << 160 << 320 << 640 << 800 << 1024;
				for(int n = 0; n < widths.count(); ++n)
				{
					if(widths[n] < sendSize.width())
					{
						widths.removeAt(n);
						--n; // adjust position
					}
				}
				GstElement *capsfilter = gst_element_factory_make("capsfilter", NULL);
				GstCaps *caps = gst_caps_new_empty();
				for(int n = 0; n < widths.count(); ++n)
				{
					GstStructure *cs;
					cs = gst_structure_new("video/x-raw-yuv",
						"width", GST_TYPE_INT_RANGE, 1, widths[n],
						"height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
					gst_caps_append_structure(caps, cs);

					cs = gst_structure_new("video/x-raw-rgb",
						"width", GST_TYPE_INT_RANGE, 1, widths[n],
						"height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
					gst_caps_append_structure(caps, cs);
				}
				GstStructure *cs = gst_structure_new("image/jpeg", NULL);
				gst_caps_append_structure(caps, cs);
				g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
				gst_caps_unref(caps);

				gst_bin_add(GST_BIN(sendPipeline), capsfilter);
				gst_element_link(videoin, capsfilter);

				videosrc = capsfilter;
			}
			//else
			//	videosrc = videoin;
		}
	}

	if(sendPipeline)
	{
		//GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(e_pipeline));
		//gst_bus_add_watch(bus, bus_call, loop);
		//gst_object_unref(bus);

		// FIXME: commenting this out because it uses too much cpu.
		//   downside is we lose looping file feature
		/*GSource *source = g_source_new(&source_funcs, sizeof(GstBusSource));
		((GstBusSource*)source)->bus = gst_pipeline_get_bus(GST_PIPELINE(sendPipeline));
		g_source_set_callback(source, (GSourceFunc)cb_bus_call, this, NULL);
		g_source_attach(source, mainContext_);
		g_source_unref(source);*/
	}

	// HACK HACK HACK, this breaks send/recv in one session
	if(sendPipeline)
		remoteAudioPayloadInfo.clear();

	if(audiosrc)
	{
		if(!addAudioChain())
		{
			error = RtpSessionContext::ErrorGeneric;
			if(cb_error)
				cb_error(app);
			return FALSE;
		}
	}
	if(videosrc)
	{
		if(!addVideoChain())
		{
			error = RtpSessionContext::ErrorGeneric;
			if(cb_error)
				cb_error(app);
			return FALSE;
		}
	}

	// FIXME: receive pipeline stuff
	{
		QString acodec, vcodec;

		if(!remoteAudioPayloadInfo.isEmpty())
		{
			printf("setting up audio recv\n");

			rpipeline = gst_pipeline_new(NULL);

			GstStructure *cs;
			GstCaps *caps;

			audiortpsrc_mutex.lock();
			audiortpsrc = gst_element_factory_make("apprtpsrc", NULL);
			audiortpsrc_mutex.unlock();
			cs = payloadInfoToStructure(remoteAudioPayloadInfo.first(), "audio");
			if(!cs)
			{
				// TODO: handle error
				printf("cannot parse payload info\n");
			}

			caps = gst_caps_new_empty();
			gst_caps_append_structure(caps, cs);
			g_object_set(G_OBJECT(audiortpsrc), "caps", caps, NULL);
			gst_caps_unref(caps);

			// FIXME: what if we don't have a name and just id?
			acodec = remoteAudioPayloadInfo.first().name.toLower();
		}

		if(!remoteVideoPayloadInfo.isEmpty())
		{
			printf("setting up video recv\n");

			rvpipeline = gst_pipeline_new(NULL);

			GstStructure *cs;
			GstCaps *caps;

			videortpsrc_mutex.lock();
			videortpsrc = gst_element_factory_make("apprtpsrc", NULL);
			videortpsrc_mutex.unlock();
			cs = payloadInfoToStructure(remoteVideoPayloadInfo.first(), "video");
			if(!cs)
			{
				// TODO: handle error
				printf("cannot parse payload info\n");
			}

			caps = gst_caps_new_empty();
			gst_caps_append_structure(caps, cs);
			g_object_set(G_OBJECT(videortpsrc), "caps", caps, NULL);
			gst_caps_unref(caps);

			// FIXME: what if we don't have a name and just id?
			vcodec = remoteVideoPayloadInfo.first().name;
			if(vcodec == "H263-1998") // FIXME: gross
				vcodec = "h263p";
			else
				vcodec = vcodec.toLower();
		}

		if(audiortpsrc)
		{
			//GstElement *audioqueue = gst_element_factory_make("queue", NULL);
			GstElement *audiortpjitterbuffer = gst_element_factory_make("gstrtpjitterbuffer", NULL);
			//GstElement *audiortpdepay = gst_element_factory_make("rtpspeexdepay", NULL);
			//GstElement *audiodec = gst_element_factory_make("speexdec", NULL);
			GstElement *audioconvert = gst_element_factory_make("audioconvert", NULL);

			{
				QMutexLocker locker(&volumeout_mutex);
				volumeout = gst_element_factory_make("volume", NULL);
				double vol = (double)outputVolume / 100;
				g_object_set(G_OBJECT(volumeout), "volume", vol, NULL);
			}

			GstElement *audioresample = gst_element_factory_make("audioresample", NULL);
			GstElement *audioout = 0;

			GstElement *audiodec = 0;
			GstElement *audiortpdepay = 0;
			audio_codec_get_recv_elements(acodec, &audiodec, &audiortpdepay);

			if(audiortpjitterbuffer)
			{
				gst_bin_add_many(GST_BIN(rpipeline), audiortpsrc, audiortpjitterbuffer, audiortpdepay, audiodec, audioconvert, volumeout, audioresample, NULL);
				gst_element_link_many(audiortpsrc, audiortpjitterbuffer, audiortpdepay, audiodec, audioconvert, volumeout, audioresample, NULL);
				g_object_set(G_OBJECT(audiortpjitterbuffer), "latency", (unsigned int)400, NULL);
			}
			else
			{
				gst_bin_add_many(GST_BIN(rpipeline), audiortpsrc, audiortpdepay, audiodec, audioconvert, volumeout, audioresample, NULL);
				gst_element_link_many(audiortpsrc, audiortpdepay, audiodec, audioconvert, volumeout, audioresample, NULL);
			}

			if(!aout.isEmpty())
			{
				printf("creating audioout\n");

				audioout = devices_makeElement(aout, PDevice::AudioOut);
				if(!audioout)
				{
					// TODO
					printf("failed to create audio output element\n");
				}
			}
			else
				audioout = gst_element_factory_make("fakesink", NULL);

			gst_bin_add(GST_BIN(rpipeline), audioout);
			gst_element_link_many(audioresample, audioout, NULL);
		}

		if(videortpsrc)
		{
			//GstElement *videoqueue = gst_element_factory_make("queue", NULL);
			GstElement *videortpjitterbuffer = gst_element_factory_make("gstrtpjitterbuffer", NULL);
			//GstElement *videortpdepay = gst_element_factory_make("rtptheoradepay", NULL);
			//GstElement *videodec = gst_element_factory_make("theoradec", NULL);
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

			GstElement *videodec = 0;
			GstElement *videortpdepay = 0;
			video_codec_get_recv_elements(vcodec, &videodec, &videortpdepay);

			if(videortpjitterbuffer)
			{
				//gst_bin_add_many(GST_BIN(rvpipeline), videortpsrc, videoqueue, videortpjitterbuffer, videortpdepay, videodec, videoconvert, videosink, NULL);
				//gst_element_link_many(videortpsrc, videoqueue, videortpjitterbuffer, videortpdepay, videodec, videoconvert, videosink, NULL);
				gst_bin_add_many(GST_BIN(rvpipeline), videortpsrc, videortpjitterbuffer, videortpdepay, videodec, videoconvert, videosink, NULL);
				gst_element_link_many(videortpsrc, videortpjitterbuffer, videortpdepay, videodec, videoconvert, videosink, NULL);

				g_object_set(G_OBJECT(videortpjitterbuffer), "latency", (unsigned int)400, NULL);
			}
			else
			{
				//gst_bin_add_many(GST_BIN(rvpipeline), videortpsrc, videoqueue, videortpdepay, videodec, videoconvert, videosink, NULL);
				//gst_element_link_many(videortpsrc, videoqueue, videortpdepay, videodec, videoconvert, videosink, NULL);
				gst_bin_add_many(GST_BIN(rvpipeline), videortpsrc, videortpdepay, videodec, videoconvert, videosink, NULL);
				gst_element_link_many(videortpsrc, videortpdepay, videodec, videoconvert, videosink, NULL);
			}
		}

		if(rpipeline)
		{
			gst_element_set_state(rpipeline, GST_STATE_READY);
			gst_element_get_state(rpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
		}

		if(rvpipeline)
		{
			gst_element_set_state(rvpipeline, GST_STATE_READY);
			gst_element_get_state(rvpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
		}

		if(rpipeline)
			gst_element_set_state(rpipeline, GST_STATE_PLAYING);

		if(rvpipeline)
			gst_element_set_state(rvpipeline, GST_STATE_PLAYING);

		printf("receive pipeline started\n");
	}

	// kickoff send pipeline
	if(sendPipeline)
	{
		if(!audiosrc && !videosrc)
		{
			// in the case of files, preroll
			gst_element_set_state(sendPipeline, GST_STATE_PAUSED);
			gst_element_get_state(sendPipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

			/*if(loopFile)
			{
				gst_element_seek(sendPipeline, 1, GST_FORMAT_TIME,
					(GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
					GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, 0);
			}*/

			return FALSE;
		}
		else
		{
			// in the case of live transmission, wait for it to start and signal
			gst_element_set_state(sendPipeline, GST_STATE_PLAYING);
			gst_element_get_state(sendPipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

			if(!getCaps())
			{
				error = RtpSessionContext::ErrorCodec;
				if(cb_error)
					cb_error(app);
				return FALSE;
			}
		}
	}

	if(cb_started)
		cb_started(app);

	return FALSE;
}

gboolean RtpWorker::doUpdate()
{
	timer = 0;

	// TODO: an actual update

	if(cb_updated)
		cb_updated(app);

	return FALSE;
}

gboolean RtpWorker::doStop()
{
	timer = 0;

	cleanup();

	if(cb_stopped)
		cb_stopped(app);

	return FALSE;
}

void RtpWorker::fileDemux_no_more_pads(GstElement *element)
{
	Q_UNUSED(element);
	printf("no more pads\n");

	// FIXME: make this get canceled on cleanup?
	GSource *ftimer = g_timeout_source_new(0);
	g_source_set_callback(ftimer, cb_fileReady, this, NULL);
	g_source_attach(ftimer, mainContext_);
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
		//GstElement *target = 0;

		bool isAudio = false;

		// FIXME: we should really just use decodebin
		if(type == "audio")
		{
			isAudio = true;

			if(subtype == "x-speex")
				decoder = gst_element_factory_make("speexdec", NULL);
			else if(subtype == "x-vorbis")
				decoder = gst_element_factory_make("vorbisdec", NULL);
		}
		else if(type == "video")
		{
			isAudio = false;

			if(subtype == "x-theora")
				decoder = gst_element_factory_make("theoradec", NULL);
		}

		if(decoder)
		{
			if(!gst_bin_add(GST_BIN(sendPipeline), decoder))
				continue;
			GstPad *sinkpad = gst_element_get_static_pad(decoder, "sink");
			if(!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(pad, sinkpad)))
				continue;
			gst_object_unref(sinkpad);

			/*GstPad *sourcepad = gst_element_get_static_pad(decoder, "src");
			sinkpad = gst_element_get_static_pad(target, "sink");
			if(!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(sourcepad, sinkpad)))
				continue;
			gst_object_unref(sourcepad);
			gst_object_unref(sinkpad);*/

			// by default the element is not in a working state.
			//   we set to 'paused' which hopefully means it'll
			//   do the right thing.
			gst_element_set_state(decoder, GST_STATE_PAUSED);

			if(isAudio)
			{
				audiosrc = decoder;
				addAudioChain();
			}
			else
			{
				videosrc = decoder;
				addVideoChain();
			}

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

void RtpWorker::videoDecode_pad_added(GstElement *element, GstPad *pad)
{
	Q_UNUSED(element);

	gchar *name = gst_pad_get_name(pad);
	printf("videoDecode pad-added: %s\n", name);
	g_free(name);

	GstCaps *caps = gst_pad_get_caps(pad);
	gchar *gstr = gst_caps_to_string(caps);
	QString capsString = QString::fromUtf8(gstr);
	g_free(gstr);
	printf("  caps: [%s]\n", qPrintable(capsString));

	GstPad *sinkpad = gst_element_get_static_pad(videoDecodeTarget, "sink");
	if(!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(pad, sinkpad)))
	{
		printf("could not link to videoDecodeTarget\n");
		gst_object_unref(sinkpad);
		gst_caps_unref(caps);
		return;
	}

	gst_object_unref(sinkpad);
	gst_caps_unref(caps);
}

gboolean RtpWorker::bus_call(GstBus *bus, GstMessage *msg)
{
	Q_UNUSED(bus);
	//GMainLoop *loop = (GMainLoop *)data;
	switch(GST_MESSAGE_TYPE(msg))
	{
		case GST_MESSAGE_EOS:
		{
			g_print("End-of-stream\n");
			//g_main_loop_quit(loop);
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

			//g_main_loop_quit(loop);
			break;
		}
		case GST_MESSAGE_SEGMENT_DONE:
		{
			// FIXME: we seem to get this event too often?
			printf("Segment-done\n");
			gst_element_seek(sendPipeline, 1, GST_FORMAT_TIME,
				(GstSeekFlags)(GST_SEEK_FLAG_SEGMENT),
				GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, 0);
			break;
		}
		default:
			break;
	}

	return TRUE;
}

void RtpWorker::show_frame_preview(int width, int height, const unsigned char *rgb32)
{
	QImage image(width, height, QImage::Format_RGB32);
	memcpy(image.bits(), rgb32, image.numBytes());

	Frame frame;
	frame.image = image;

	if(cb_previewFrame)
		cb_previewFrame(frame, app);
}

void RtpWorker::show_frame_output(int width, int height, const unsigned char *rgb32)
{
	QImage image(width, height, QImage::Format_RGB32);
	memcpy(image.bits(), rgb32, image.numBytes());

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

	QMutexLocker locker(&rtpaudioout_mutex);
	if(cb_rtpAudioOut && rtpaudioout)
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

	QMutexLocker locker(&rtpvideoout_mutex);
	if(cb_rtpVideoOut && rtpvideoout)
		cb_rtpVideoOut(packet, app);
}

gboolean RtpWorker::fileReady()
{
	if(loopFile)
	{
		//gst_element_set_state(sendPipeline, GST_STATE_PAUSED);
		//gst_element_get_state(sendPipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

		gst_element_seek(sendPipeline, 1, GST_FORMAT_TIME,
			(GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
			GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, 0);
	}

	gst_element_set_state(sendPipeline, GST_STATE_PLAYING);
	gst_element_get_state(sendPipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

	if(!getCaps())
	{
		error = RtpSessionContext::ErrorCodec;
		if(cb_error)
			cb_error(app);
		return FALSE;
	}

	if(cb_started)
		cb_started(app);
	return FALSE;
}

bool RtpWorker::addAudioChain()
{
	// FIXME: what if the user wanted to use payloadinfo instead of
	//   params?
	if(localAudioParams.isEmpty())
		return false;
	QString codec = localAudioParams[0].codec;
	int rate = localAudioParams[0].sampleRate;
	int size = localAudioParams[0].sampleSize;
	int channels = localAudioParams[0].channels;
	printf("codec=%s\n", qPrintable(codec));

	GstElement *audioenc = 0;
	audiortppay = 0;
	if(!audio_codec_get_send_elements(codec, &audioenc, &audiortppay))
		return false;

	if(desired_speex_pt != -1)
	{
		printf("setting speex payloader to use id %d\n", desired_speex_pt);
		g_object_set(G_OBJECT(audiortppay), "pt", desired_speex_pt, NULL);
	}

	GstElement *queue = gst_element_factory_make("queue", NULL);

	{
		QMutexLocker locker(&volumein_mutex);
		volumein = gst_element_factory_make("volume", NULL);
		double vol = (double)inputVolume / 100;
		g_object_set(G_OBJECT(volumein), "volume", vol, NULL);
	}

	GstElement *audioconvert = gst_element_factory_make("audioconvert", NULL);
	GstElement *audioresample = gst_element_factory_make("audioresample", NULL);

	GstCaps *caps = gst_caps_new_simple("audio/x-raw-int",
		"rate", G_TYPE_INT, rate,
		"width", G_TYPE_INT, size,
		"channels", G_TYPE_INT, channels, NULL);

	GstElement *audiortpsink = gst_element_factory_make("apprtpsink", NULL);
	GstAppRtpSink *appRtpSink = (GstAppRtpSink *)audiortpsink;
	appRtpSink->appdata = this;
	appRtpSink->packet_ready = cb_packet_ready_rtp_audio;
	// TODO: does sync matter?
	//g_object_set(G_OBJECT(appRtpSink), "sync", TRUE, NULL);

	gst_bin_add(GST_BIN(sendPipeline), queue);
	gst_bin_add(GST_BIN(sendPipeline), volumein);
	gst_bin_add(GST_BIN(sendPipeline), audioconvert);
	gst_bin_add(GST_BIN(sendPipeline), audioresample);
	gst_bin_add(GST_BIN(sendPipeline), audioenc);
	gst_bin_add(GST_BIN(sendPipeline), audiortppay);
	gst_bin_add(GST_BIN(sendPipeline), audiortpsink);

	gst_element_link_many(queue, volumein, audioconvert, audioresample, NULL);
	gst_element_link_filtered(audioresample, audioenc, caps);
	gst_element_link_many(audioenc, audiortppay, audiortpsink, NULL);

	gst_element_set_state(queue, GST_STATE_PAUSED);
	gst_element_set_state(volumein, GST_STATE_PAUSED);
	gst_element_set_state(audioconvert, GST_STATE_PAUSED);
	gst_element_set_state(audioresample, GST_STATE_PAUSED);
	gst_element_set_state(audioenc, GST_STATE_PAUSED);
	gst_element_set_state(audiortppay, GST_STATE_PAUSED);
	gst_element_set_state(audiortpsink, GST_STATE_PAUSED);

	gst_element_link(audiosrc, queue);
	return true;
}

bool RtpWorker::addVideoChain()
{
	// FIXME: what if the user wanted to use payloadinfo instead of
	//   params?
	if(localVideoParams.isEmpty())
		return false;
	QString codec = localVideoParams[0].codec;
	QSize size = localVideoParams[0].size;
	int fps = localVideoParams[0].fps;
	printf("codec=%s\n", qPrintable(codec));

	GstElement *videoenc = 0;
	videortppay = 0;
	if(!video_codec_get_send_elements(codec, &videoenc, &videortppay))
		return false;

	GstElement *queue = gst_element_factory_make("queue", NULL);
	videoDecode = gst_element_factory_make("decodebin", NULL);
	g_signal_connect(G_OBJECT(videoDecode),
		"pad-added",
		G_CALLBACK(cb_videoDecode_pad_added), this);
	GstElement *videoconvertprep = gst_element_factory_make("ffmpegcolorspace", NULL);
	videoDecodeTarget = videoconvertprep;
	GstElement *videorate;
	if(fileDemux)
		videorate = gst_element_factory_make("videorate", NULL);
	else // use videomaxrate for live sources
		videorate = gst_element_factory_make("videomaxrate", NULL);
	GstElement *videoscale = gst_element_factory_make("videoscale", NULL);
	GstElement *videotee = gst_element_factory_make("tee", NULL);

	GstElement *playqueue = gst_element_factory_make("queue", NULL);
	GstElement *videoconvertplay = gst_element_factory_make("ffmpegcolorspace", NULL);
	GstElement *videoplaysink = gst_element_factory_make("appvideosink", NULL);
	GstAppVideoSink *appVideoSink = (GstAppVideoSink *)videoplaysink;
	appVideoSink->appdata = this;
	appVideoSink->show_frame = cb_show_frame_preview;

	GstElement *rtpqueue = gst_element_factory_make("queue", NULL);
	GstElement *videoconvertrtp = gst_element_factory_make("ffmpegcolorspace", NULL);
	GstElement *videortpsink = gst_element_factory_make("apprtpsink", NULL);
	GstAppRtpSink *appRtpSink = (GstAppRtpSink *)videortpsink;
	appRtpSink->appdata = this;
	appRtpSink->packet_ready = cb_packet_ready_rtp_video;
	// TODO: does sync matter?
	//g_object_set(G_OBJECT(appRtpSink), "sync", TRUE, NULL);

	gst_bin_add(GST_BIN(sendPipeline), queue);
	gst_bin_add(GST_BIN(sendPipeline), videoDecode);
	gst_bin_add(GST_BIN(sendPipeline), videoconvertprep);
	gst_bin_add(GST_BIN(sendPipeline), videorate);
	gst_bin_add(GST_BIN(sendPipeline), videoscale);
	gst_bin_add(GST_BIN(sendPipeline), videotee);
	gst_bin_add(GST_BIN(sendPipeline), playqueue);
	gst_bin_add(GST_BIN(sendPipeline), videoconvertplay);
	gst_bin_add(GST_BIN(sendPipeline), videoplaysink);
	gst_bin_add(GST_BIN(sendPipeline), rtpqueue);
	gst_bin_add(GST_BIN(sendPipeline), videoconvertrtp);
	gst_bin_add(GST_BIN(sendPipeline), videoenc);
	gst_bin_add(GST_BIN(sendPipeline), videortppay);
	gst_bin_add(GST_BIN(sendPipeline), videortpsink);

	if(!gst_element_link(queue, videoDecode))
		return false;

	// FIXME: i don't know how to set up caps filters without knowing
	//   the mime subtype, so for now we'll (perhaps stupidly) force
	//   ourselves into a fixed subtype that is therefore known.
	GstCaps *yuvcaps = gst_caps_new_simple("video/x-raw-yuv", NULL);
	if(!gst_element_link_filtered(videoconvertprep, videorate, yuvcaps))
	{
		return false;
	}

	GstCaps *ratecaps = gst_caps_new_simple("video/x-raw-yuv",
		"framerate", GST_TYPE_FRACTION, fps, 1,
		NULL);
	if(!gst_element_link_filtered(videorate, videoscale, ratecaps))
	{
		return false;
	}

	GstCaps *scalecaps = gst_caps_new_simple("video/x-raw-yuv",
		"width", G_TYPE_INT, size.width(),
		"height", G_TYPE_INT, size.height(),
		NULL);
	if(!gst_element_link_filtered(videoscale, videotee, scalecaps))
	{
		return false;
	}

	if(!gst_element_link_many(videotee, playqueue, videoconvertplay,
		videoplaysink, NULL))
	{
		return false;
	}

	if(!gst_element_link_many(videotee, rtpqueue, videoconvertrtp, videoenc,
		videortppay, videortpsink, NULL))
	{
		return false;
	}

	gst_element_set_state(queue, GST_STATE_PAUSED);
	gst_element_set_state(videoDecode, GST_STATE_PAUSED);
	gst_element_set_state(videoconvertprep, GST_STATE_PAUSED);
	gst_element_set_state(videorate, GST_STATE_PAUSED);
	gst_element_set_state(videoscale, GST_STATE_PAUSED);
	gst_element_set_state(videotee, GST_STATE_PAUSED);
	gst_element_set_state(playqueue, GST_STATE_PAUSED);
	gst_element_set_state(videoconvertplay, GST_STATE_PAUSED);
	gst_element_set_state(videoplaysink, GST_STATE_PAUSED);
	gst_element_set_state(rtpqueue, GST_STATE_PAUSED);
	gst_element_set_state(videoconvertrtp, GST_STATE_PAUSED);
	gst_element_set_state(videoenc, GST_STATE_PAUSED);
	gst_element_set_state(videortppay, GST_STATE_PAUSED);
	gst_element_set_state(videortpsink, GST_STATE_PAUSED);

	localVideoPayloadInfo = QList<PPayloadInfo>() << PPayloadInfo();
	canTransmitVideo = true;

	gst_element_link(videosrc, queue);
	return true;
}

bool RtpWorker::getCaps()
{
	if(audiortppay)
	{
		GstPad *pad = gst_element_get_static_pad(audiortppay, "src");
		GstCaps *caps = gst_pad_get_negotiated_caps(pad);
		if(!caps)
		{
			printf("can't get audio caps\n");
			return false;
		}
		gchar *gstr = gst_caps_to_string(caps);
		QString capsString = QString::fromUtf8(gstr);
		g_free(gstr);
		printf("rtppay caps audio: [%s]\n", qPrintable(capsString));
		gst_object_unref(pad);

		GstStructure *cs = gst_caps_get_structure(caps, 0);

		PPayloadInfo pi = structureToPayloadInfo(cs);
		if(pi.id == -1)
		{
			gst_caps_unref(caps);
			return false;
		}

		gst_caps_unref(caps);

		localAudioPayloadInfo = QList<PPayloadInfo>() << pi;
		canTransmitAudio = true;
	}

	if(videortppay)
	{
		GstPad *pad = gst_element_get_static_pad(videortppay, "src");
		GstCaps *caps = gst_pad_get_negotiated_caps(pad);
		if(!caps)
		{
			printf("can't get video caps\n");
			return false;
		}
		gchar *gstr = gst_caps_to_string(caps);
		QString capsString = QString::fromUtf8(gstr);
		g_free(gstr);
		printf("rtppay caps video: [%s]\n", qPrintable(capsString));
		gst_object_unref(pad);

		GstStructure *cs = gst_caps_get_structure(caps, 0);

		PPayloadInfo pi = structureToPayloadInfo(cs);
		if(pi.id == -1)
		{
			gst_caps_unref(caps);
			return false;
		}

		gst_caps_unref(caps);

		localVideoPayloadInfo = QList<PPayloadInfo>() << pi;
		canTransmitVideo = true;
	}

	// FIXME: this is way wrong
	bool okcodecs = true;
	if(!localAudioPayloadInfo.isEmpty() && !remoteAudioPayloadInfo.isEmpty())
	{
		// reduce to 1 item
		remoteAudioPayloadInfo = QList<PPayloadInfo>() << remoteAudioPayloadInfo.first();

		if(!compare_PPayloadInfo(localAudioPayloadInfo.first(), remoteAudioPayloadInfo.first()))
		{
			okcodecs = false;
		}
	}
	if(!localVideoPayloadInfo.isEmpty() && !remoteVideoPayloadInfo.isEmpty())
	{
		// reduce to 1 item
		remoteVideoPayloadInfo = QList<PPayloadInfo>() << remoteVideoPayloadInfo.first();

		if(!compare_PPayloadInfo(localVideoPayloadInfo.first(), remoteVideoPayloadInfo.first()))
		{
			okcodecs = false;
		}
	}
	if(!okcodecs)
		return false;

	return true;
}

}
