/*
 * Copyright (C) 2008-2009  Barracuda Networks, Inc.
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

#include <stdio.h>
#include <gst/app/gstappsrc.h>
#include <QStringList>
#include <QTime>
#include <cstring>

#include "devices.h"
#include "payloadinfo.h"
#include "pipeline.h"
#include "bins.h"

// TODO: support playing from bytearray
// TODO: support recording

#define RTPWORKER_DEBUG

namespace PsiMedia {

static GstStaticPadTemplate raw_audio_src_template = GST_STATIC_PAD_TEMPLATE("src",
                                                                             GST_PAD_SRC,
                                                                             GST_PAD_ALWAYS,
                                                                             GST_STATIC_CAPS(
                                                                                 "audio/x-raw"
                                                                                 )
                                                                             );

static GstStaticPadTemplate raw_audio_sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                              GST_PAD_SINK,
                                                                              GST_PAD_ALWAYS,
                                                                              GST_STATIC_CAPS(
                                                                                  "audio/x-raw"
                                                                                  )
                                                                              );

static GstStaticPadTemplate raw_video_sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                              GST_PAD_SINK,
                                                                              GST_PAD_ALWAYS,
                                                                              GST_STATIC_CAPS(
                                                                                  "video/x-raw"
                                                                                  )
                                                                              );

static const char *state_to_str(GstState state)
{
    switch(state)
    {
    case GST_STATE_NULL:    return "NULL";
    case GST_STATE_READY:   return "READY";
    case GST_STATE_PAUSED:  return "PAUSED";
    case GST_STATE_PLAYING: return "PLAYING";
    case GST_STATE_VOID_PENDING:
    default:
        return 0;
    }
}

class Stats
{
public:
    QString name;
    int calls;
    int sizes[30];
    int sizes_at;
    QTime calltime;

    Stats(const QString &_name) :
        name(_name),
        calls(-1),
        sizes_at(0)
    {
        for(int k = 0; k < 30; ++k)
            sizes[k] = 0;
    }

    void print_stats(int current_size)
    {
        // -2 means quit
        if(calls == -2)
            return;

        if(sizes_at >= 30)
        {
            memmove(sizes, sizes + 1, sizeof(int) * (sizes_at - 1));
            --sizes_at;
        }
        sizes[sizes_at++] = current_size;

        // set timer on first call
        if(calls == -1)
        {
            calls = 0;
            calltime.start();
        }

        // print bitrate after 10 seconds
        if(calltime.elapsed() >= 10000)
        {
            int avg = 0;
            for(int n = 0; n < sizes_at; ++n)
                avg += sizes[n];
            avg /= sizes_at;
            int bytesPerSec = (calls * avg) / 10;
            int bps = bytesPerSec * 10;
            int kbps = bps / 1000;
            calls = -2;
            calltime.restart();
            qDebug("%s: average packet size=%d, kbps=%d\n", qPrintable(name), avg, kbps);
        }
        else
            ++calls;
    }
};

#ifdef RTPWORKER_DEBUG
static void dump_pipeline(GstElement *in, int indent = 1);
static void dump_pipeline_each(const GValue *value, gpointer data)
{
    GstElement *e = (GstElement *)g_value_get_object(value);
    int indent = *((int*)data);
    if(GST_IS_BIN(e))
    {
        qDebug("%s%s:\n", qPrintable(QString(indent, ' ')), gst_element_get_name(e));
        dump_pipeline(e, indent + 2);
    }
    else
        qDebug("%s%s\n", qPrintable(QString(indent, ' ')), gst_element_get_name(e));
}

static void dump_pipeline(GstElement *in, int indent)
{
    GstIterator *it = gst_bin_iterate_elements(GST_BIN(in));
    gst_iterator_foreach(it, dump_pipeline_each, &indent);
    gst_iterator_free(it);
}
#endif

//----------------------------------------------------------------------------
// RtpWorker
//----------------------------------------------------------------------------
static int worker_refs = 0;
static PipelineContext *send_pipelineContext = 0;
static PipelineContext *recv_pipelineContext = 0;
static GstElement *spipeline = 0;
static GstElement *rpipeline = 0;
//static GstBus *sbus = 0;
static bool send_in_use = false;
static bool recv_in_use = false;

static bool use_shared_clock = true;
static GstClock *shared_clock = 0;
static bool send_clock_is_shared = false;
//static bool recv_clock_is_shared = false;

RtpWorker::RtpWorker(GMainContext *mainContext) :
    app(0),
    loopFile(false),
    maxbitrate(-1),
    canTransmitAudio(false),
    canTransmitVideo(false),
    outputVolume(100),
    inputVolume(100),
    error(0),
    cb_started(0),
    cb_updated(0),
    cb_stopped(0),
    cb_finished(0),
    cb_error(0),
    cb_audioOutputIntensity(0),
    cb_audioInputIntensity(0),
    cb_previewFrame(0),
    cb_outputFrame(0),
    cb_rtpAudioOut(0),
    cb_rtpVideoOut(0),
    cb_recordData(0),
    mainContext_(mainContext),
    timer(0),
    pd_audiosrc(0),
    pd_videosrc(0),
    pd_audiosink(0),
    sendbin(0),
    recvbin(0),
    fileDemux(0),
    audiosrc(0),
    videosrc(0),
    audiortpsrc(0),
    videortpsrc(0),
    audiortppay(0),
    videortppay(0),
    volumein(0),
    volumeout(0),
    rtpaudioout(false),
    rtpvideoout(false)
  //recordTimer(0)
{
    audioStats = new Stats("audio");
    videoStats = new Stats("video");

    if(worker_refs == 0)
    {
        send_pipelineContext = new PipelineContext;
        recv_pipelineContext = new PipelineContext;

        spipeline = send_pipelineContext->element();
        rpipeline = recv_pipelineContext->element();

#ifdef RTPWORKER_DEBUG
        /*sbus = gst_pipeline_get_bus(GST_PIPELINE(spipeline));
        GSource *source = gst_bus_create_watch(bus);
        gst_object_unref(bus);
        g_source_set_callback(source, (GSourceFunc)cb_bus_call, this, NULL);
        g_source_attach(source, mainContext_);*/
#endif

        QByteArray val = qgetenv("PSI_NO_SHARED_CLOCK");
        if(!val.isEmpty())
            use_shared_clock = false;
    }

    ++worker_refs;
}

RtpWorker::~RtpWorker()
{
    if(timer)
    {
        g_source_destroy(timer);
        timer = 0;
    }

    /*if(recordTimer)
    {
        g_source_destroy(recordTimer);
        recordTimer = 0;
    }*/

    cleanup();

    --worker_refs;
    if(worker_refs == 0)
    {
        delete send_pipelineContext;
        send_pipelineContext = 0;

        delete recv_pipelineContext;
        recv_pipelineContext = 0;

        //sbus = 0;
    }

    delete audioStats;
    delete videoStats;
}

void RtpWorker::cleanup()
{
#ifdef RTPWORKER_DEBUG
    qDebug("cleaning up...\n");
#endif
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

    //if(pd_audiosrc)
    //	pd_audiosrc->deactivate();

    //if(pd_videosrc)
    //	pd_videosrc->deactivate();

    if(sendbin)
    {
        if(shared_clock && send_clock_is_shared)
        {
            gst_object_unref(shared_clock);
            shared_clock = 0;
            send_clock_is_shared = false;

            if(recv_in_use)
            {
                qDebug("recv clock reverts to auto\n");
                gst_element_set_state(rpipeline, GST_STATE_READY);
                gst_element_get_state(rpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
                gst_pipeline_auto_clock(GST_PIPELINE(rpipeline));

                // only restart the receive pipeline if it is
                //   owned by a separate session
                if(!recvbin)
                {
                    gst_element_set_state(rpipeline, GST_STATE_PLAYING);
                    //gst_element_get_state(rpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
                }
            }
        }

        send_pipelineContext->deactivate();
        gst_pipeline_auto_clock(GST_PIPELINE(spipeline));
        //gst_element_set_state(sendbin, GST_STATE_NULL);
        //gst_element_get_state(sendbin, NULL, NULL, GST_CLOCK_TIME_NONE);
        gst_bin_remove(GST_BIN(spipeline), sendbin);
        sendbin = 0;
        send_in_use = false;
    }

    if(recvbin)
    {
        // NOTE: commenting this out because recv clock is no longer
        //  ever shared
        /*if(shared_clock && recv_clock_is_shared)
        {
            gst_object_unref(shared_clock);
            shared_clock = 0;
            recv_clock_is_shared = false;

            if(send_in_use)
            {
                // FIXME: do we really need to restart the pipeline?

                qDebug("send clock becomes master\n");
                send_pipelineContext->deactivate();
                gst_pipeline_auto_clock(GST_PIPELINE(spipeline));
                send_pipelineContext->activate();
                //gst_element_get_state(spipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

                // send clock becomes shared
                shared_clock = gst_pipeline_get_clock(GST_PIPELINE(spipeline));
                gst_object_ref(GST_OBJECT(shared_clock));
                gst_pipeline_use_clock(GST_PIPELINE(spipeline), shared_clock);
                send_clock_is_shared = true;
            }
        }*/

        recv_pipelineContext->deactivate();
        gst_pipeline_auto_clock(GST_PIPELINE(rpipeline));
        //gst_element_set_state(recvbin, GST_STATE_NULL);
        //gst_element_get_state(recvbin, NULL, NULL, GST_CLOCK_TIME_NONE);
        gst_bin_remove(GST_BIN(rpipeline), recvbin);
        recvbin = 0;
        recv_in_use = false;
    }

    if(pd_audiosrc)
    {
        delete pd_audiosrc;
        pd_audiosrc = 0;
        audiosrc = 0;
    }

    if(pd_videosrc)
    {
        delete pd_videosrc;
        pd_videosrc = 0;
        videosrc = 0;
    }

    if(pd_audiosink)
    {
        delete pd_audiosink;
        pd_audiosink = 0;
    }

#ifdef RTPWORKER_DEBUG
    qDebug("cleaning done.\n");
#endif
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
    // cancel any current operation
    if(timer)
        g_source_destroy(timer);

    timer = g_timeout_source_new(0);
    g_source_set_callback(timer, cb_doStop, this, NULL);
    g_source_attach(timer, mainContext_);
}

static GstBuffer* makeGstBuffer(const PRtpPacket &packet)
{
    GstBuffer *buffer;
    GstMemory *memory;
    GstMapInfo info;
    buffer = gst_buffer_new ();
    memory = gst_allocator_alloc (NULL, packet.rawValue.size(), NULL);
    if (buffer && memory) {
        gst_memory_map(memory, &info, GST_MAP_WRITE);
        std::memcpy(info.data, packet.rawValue.data(), packet.rawValue.size());
        gst_memory_unmap(memory, &info);
        gst_buffer_insert_memory (buffer, -1, memory);
        return buffer;
    }
    if (memory) {
        gst_allocator_free(NULL, memory);
    }
    if (buffer) {
        gst_buffer_unref(buffer);
    }
    return nullptr;
}

GstAppSink* RtpWorker::makeVideoPlayAppSink(const gchar *name)
{
    GstElement *videoplaysink = gst_element_factory_make("appsink", name); // was appvideosink
    GstAppSink *appVideoSink = reinterpret_cast<GstAppSink *>(videoplaysink);

    GstCaps *videoplaycaps;
    videoplaycaps = gst_caps_new_simple ("video/x-raw",
                                         "format", G_TYPE_STRING, "BGRx",
                                         NULL);
    gst_app_sink_set_caps (appVideoSink, videoplaycaps);
    gst_caps_unref(videoplaycaps);

    return appVideoSink;
}

void RtpWorker::rtpAudioIn(const PRtpPacket &packet)
{
    QMutexLocker locker(&audiortpsrc_mutex);
    if(packet.portOffset == 0 && audiortpsrc) {
        gst_app_src_push_buffer((GstAppSrc *)audiortpsrc, makeGstBuffer(packet));
    }
}

void RtpWorker::rtpVideoIn(const PRtpPacket &packet)
{
    QMutexLocker locker(&videortpsrc_mutex);
    if(packet.portOffset == 0 && videortpsrc)
        gst_app_src_push_buffer((GstAppSrc *)videortpsrc, makeGstBuffer(packet));
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

gboolean RtpWorker::cb_bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    return ((RtpWorker *)data)->bus_call(bus, msg);
}

GstFlowReturn RtpWorker::cb_show_frame_preview(GstAppSink *appsink, gpointer data)
{
    return ((RtpWorker *)data)->show_frame_preview(appsink);
}

GstFlowReturn RtpWorker::cb_show_frame_output(GstAppSink *appsink, gpointer data)
{
    return ((RtpWorker *)data)->show_frame_output(appsink);
}

GstFlowReturn RtpWorker::cb_packet_ready_rtp_audio(GstAppSink *appsink, gpointer data)
{
    return ((RtpWorker *)data)->packet_ready_rtp_audio(appsink);
}

GstFlowReturn RtpWorker::cb_packet_ready_rtp_video(GstAppSink *appsink, gpointer data)
{
    return ((RtpWorker *)data)->packet_ready_rtp_video(appsink);
}

GstFlowReturn RtpWorker::cb_packet_ready_preroll_stub(GstAppSink *appsink, gpointer data)
{
    Q_UNUSED(appsink)
    Q_UNUSED(data)
    qDebug("RtpWorker::cb_packet_ready_preroll_stub");
    return GST_FLOW_OK;
}

void RtpWorker::cb_packet_ready_eos_stub(GstAppSink *appsink, gpointer data)
{
    Q_UNUSED(appsink)
    Q_UNUSED(data)
    qDebug("RtpWorker::cb_packet_ready_eos_stub");
}


gboolean RtpWorker::cb_fileReady(gpointer data)
{
    return ((RtpWorker *)data)->fileReady();
}

gboolean RtpWorker::doStart()
{
    timer = 0;

    fileDemux = 0;
    audiosrc = 0;
    videosrc = 0;
    audiortpsrc = 0;
    videortpsrc = 0;
    audiortppay = 0;
    videortppay = 0;

    // default to 400kbps
    if(maxbitrate == -1)
        maxbitrate = 400;

    if(!setupSendRecv())
    {
        if(cb_error)
            cb_error(app);
    }
    else
    {
        // don't signal started here if using files
        if(!fileDemux && cb_started)
            cb_started(app);
    }

    return FALSE;
}

gboolean RtpWorker::doUpdate()
{
    timer = 0;

    if(!setupSendRecv())
    {
        if(cb_error)
            cb_error(app);
    }
    else
    {
        if(cb_updated)
            cb_updated(app);
    }

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
#ifdef RTPWORKER_DEBUG
    qDebug("no more pads\n");
#endif

    // FIXME: make this get canceled on cleanup?
    GSource *ftimer = g_timeout_source_new(0);
    g_source_set_callback(ftimer, cb_fileReady, this, NULL);
    g_source_attach(ftimer, mainContext_);
}

void RtpWorker::fileDemux_pad_added(GstElement *element, GstPad *pad)
{
    Q_UNUSED(element);

#ifdef RTPWORKER_DEBUG
    gchar *name = gst_pad_get_name(pad);
    qDebug("pad-added: %s\n", name);
    g_free(name);
#endif

    GstCaps *caps = gst_pad_query_caps(pad, NULL);
#ifdef RTPWORKER_DEBUG
    gchar *gstr = gst_caps_to_string(caps);
    QString capsString = QString::fromUtf8(gstr);
    g_free(gstr);
    qDebug("  caps: [%s]\n", qPrintable(capsString));
#endif

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

        bool isAudio = false;

        // FIXME: we should really just use decodebin
        if(type == "audio")
        {
            isAudio = true;

            if(subtype == "x-opus")
                decoder = gst_element_factory_make("opusdec", NULL);
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
            if(!gst_bin_add(GST_BIN(sendbin), decoder))
                continue;
            GstPad *sinkpad = gst_element_get_static_pad(decoder, "sink");
            if(!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(pad, sinkpad)))
                continue;
            gst_object_unref(sinkpad);

            // FIXME
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

    // TODO: do we need to do anything here?

#ifdef RTPWORKER_DEBUG
    gchar *name = gst_pad_get_name(pad);
    qDebug("pad-removed: %s\n", name);
    g_free(name);
#endif
}

gboolean RtpWorker::bus_call(GstBus *bus, GstMessage *msg)
{
    Q_UNUSED(bus);
    //GMainLoop *loop = static_cast<GMainLoop *>(data);
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

        g_print("Error: %s: %s\n", gst_element_get_name(GST_MESSAGE_SRC(msg)), err->message);
        g_error_free(err);

        //g_main_loop_quit(loop);
        break;
    }
    case GST_MESSAGE_SEGMENT_DONE:
    {
        // FIXME: we seem to get this event too often?
        qDebug("Segment-done\n");
        /*gst_element_seek(sendPipeline, 1, GST_FORMAT_TIME,
                (GstSeekFlags)(GST_SEEK_FLAG_SEGMENT),
                GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, 0);*/
        break;
    }
    case GST_MESSAGE_WARNING:
    {
        gchar *debug;
        GError *err;

        gst_message_parse_warning(msg, &err, &debug);
        g_free(debug);

        g_print("Warning: %s: %s\n", gst_element_get_name(GST_MESSAGE_SRC(msg)), err->message);
        g_error_free(err);

        //g_main_loop_quit(loop);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
        GstState oldstate, newstate, pending;

        gst_message_parse_state_changed(msg, &oldstate, &newstate, &pending);
        qDebug("State changed: %s: %s->%s",
               gst_element_get_name(GST_MESSAGE_SRC(msg)),
               state_to_str(oldstate),
               state_to_str(newstate));
        if(pending != GST_STATE_VOID_PENDING)
            qDebug(" (%s)", state_to_str(pending));
        qDebug("\n");
        break;
    }
    case GST_MESSAGE_ASYNC_DONE:
    {
        qDebug("Async done: %s\n", gst_element_get_name(GST_MESSAGE_SRC(msg)));
        break;
    }
    default:
        qDebug("Bus message: %s\n", GST_MESSAGE_TYPE_NAME(msg));
        break;
    }

    return TRUE;
}

GstFlowReturn RtpWorker::show_frame_preview(GstAppSink *appsink)
{
    Frame frame = Frame::pullFromSink(appsink);
    if (frame.image.isNull()) {
        return GST_FLOW_ERROR;
    }

    if(cb_previewFrame)
        cb_previewFrame(frame, app);

    return GST_FLOW_OK;
}

GstFlowReturn RtpWorker::show_frame_output(GstAppSink *appsink)
{
    Frame frame = Frame::pullFromSink(appsink);
    if (frame.image.isNull()) {
        return GST_FLOW_ERROR;
    }

    if(cb_outputFrame)
        cb_outputFrame(frame, app);

    return GST_FLOW_OK;
}

GstFlowReturn RtpWorker::packet_ready_rtp_audio(GstAppSink *appsink)
{
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    int sz = gst_buffer_get_size(buffer);
    QByteArray ba;
    ba.resize(sz);
    gst_buffer_extract(buffer, 0, ba.data(), sz);
    gst_sample_unref(sample);

    PRtpPacket packet;
    packet.rawValue = ba;
    packet.portOffset = 0;

#ifdef RTPWORKER_DEBUG
    audioStats->print_stats(packet.rawValue.size());
#endif

    QMutexLocker locker(&rtpaudioout_mutex);
    if(cb_rtpAudioOut && rtpaudioout)
        cb_rtpAudioOut(packet, app);

    return GST_FLOW_OK;
}

GstFlowReturn RtpWorker::packet_ready_rtp_video(GstAppSink *appsink)
{
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    int sz = gst_buffer_get_size(buffer);
    QByteArray ba;
    ba.resize(sz);
    gst_buffer_extract(buffer, 0, ba.data(), sz);
    gst_sample_unref(sample);

    PRtpPacket packet;
    packet.rawValue = ba;
    packet.portOffset = 0;

#ifdef RTPWORKER_DEBUG
    videoStats->print_stats(packet.rawValue.size());
#endif

    QMutexLocker locker(&rtpvideoout_mutex);
    if(cb_rtpVideoOut && rtpvideoout)
        cb_rtpVideoOut(packet, app);

    return GST_FLOW_OK;
}

gboolean RtpWorker::fileReady()
{
    if(loopFile)
    {
        //gst_element_set_state(sendPipeline, GST_STATE_PAUSED);
        //gst_element_get_state(sendPipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

        /*gst_element_seek(sendPipeline, 1, GST_FORMAT_TIME,
            (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
            GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, 0);*/
    }

    send_pipelineContext->activate();
    gst_element_get_state(send_pipelineContext->element(), NULL, NULL, GST_CLOCK_TIME_NONE);
    //gst_element_set_state(sendPipeline, GST_STATE_PLAYING);
    //gst_element_get_state(sendPipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

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

bool RtpWorker::setupSendRecv()
{
    // FIXME:
    // this code is not really correct, but it will suffice for our
    //   modest purposes.  basically the way it works is:
    //   - non-empty params indicate desire for a media type
    //   - the only control you have over quality is maxbitrate
    //   - input device/file indicates desire to send
    //   - remote payloadinfo indicates desire to receive (we need this
    //     to support theora)
    //   - once sending or receiving is started, media types cannot
    //     be added or removed (doing so will throw an error)
    //   - once sending or receiving is started, codecs can't be changed
    //     (changes will be rejected).  one exception: remote theora
    //     config can be updated.
    //   - once sending or receiving is started, devices can't be changed
    //     (changes will be ignored)

    if(!sendbin)
    {
        if(!localAudioParams.isEmpty() || !localVideoParams.isEmpty())
        {
            if(!startSend())
                return false;
        }
    }
    else
    {
        // TODO: support adding/removing audio/video to existing session
        /*if((localAudioParams.isEmpty() != actual_localAudioPayloadInfo.isEmpty()) || (localVideoParams.isEmpty() != actual_videoPayloadInfo.isEmpty()))
        {
            error = RtpSessionContext::ErrorGeneric;
            return false;
        }*/
    }

    if(!recvbin)
    {
        if((!localAudioParams.isEmpty() && !remoteAudioPayloadInfo.isEmpty()) || (!localVideoParams.isEmpty() && !remoteVideoPayloadInfo.isEmpty()))
        {
            if(!startRecv())
                return false;
        }
    }
    else
    {
        // TODO: support adding/removing audio/video to existing session

        // see if theora was updated in the remote config
        updateTheoraConfig();
    }

    // apply actual settings back to these variables, so the user can
    //   read them
    localAudioPayloadInfo = actual_localAudioPayloadInfo;
    localVideoPayloadInfo = actual_localVideoPayloadInfo;
    remoteAudioPayloadInfo = actual_remoteAudioPayloadInfo;
    remoteVideoPayloadInfo = actual_remoteVideoPayloadInfo;

    return true;
}

bool RtpWorker::startSend()
{
    return startSend(16000);
}

bool RtpWorker::startSend(int rate)
{
    // file source
    if(!infile.isEmpty() || !indata.isEmpty())
    {
        if(send_in_use)
            return false;

        sendbin = gst_bin_new("sendbin");

        GstElement *fileSource = gst_element_factory_make("filesrc", NULL);
        g_object_set(G_OBJECT(fileSource), "location", infile.toUtf8().data(), NULL);

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

        gst_bin_add(GST_BIN(sendbin), fileSource);
        gst_bin_add(GST_BIN(sendbin), fileDemux);
        gst_element_link(fileSource, fileDemux);
    }
    // device source
    else if(!ain.isEmpty() || !vin.isEmpty())
    {
        if(send_in_use)
            return false;

        sendbin = gst_bin_new("sendbin");

        if(!ain.isEmpty() && !localAudioParams.isEmpty())
        {
            PipelineDeviceOptions options;
            if (pd_audiosink != nullptr) {
                options = pd_audiosink->options();
                options.aec = !options.echoProberName.isEmpty();
            }

            pd_audiosrc = PipelineDeviceContext::create(send_pipelineContext, ain, PDevice::AudioIn, options);
            if(!pd_audiosrc)
            {
#ifdef RTPWORKER_DEBUG
                qDebug("Failed to create audio input element '%s'.\n", qPrintable(ain));
#endif
                g_object_unref(G_OBJECT(sendbin));
                sendbin = 0;

                error = RtpSessionContext::ErrorGeneric;
                return false;
            }
            audiosrc = pd_audiosrc->element();
        }

        if(!vin.isEmpty() && !localVideoParams.isEmpty())
        {
            PipelineDeviceOptions opts;
            //opts.videoSize = localVideoParams[0].size;
            opts.videoSize = QSize(640, 480);
            opts.fps = 30;

            pd_videosrc = PipelineDeviceContext::create(send_pipelineContext, vin, PDevice::VideoIn, opts);
            if(!pd_videosrc)
            {
#ifdef RTPWORKER_DEBUG
                qDebug("Failed to create video input element '%s'.\n", qPrintable(vin));
#endif
                delete pd_audiosrc;
                pd_audiosrc = 0;
                g_object_unref(G_OBJECT(sendbin));
                sendbin = 0;

                error = RtpSessionContext::ErrorGeneric;
                return false;
            }

            videosrc = pd_videosrc->element();
        }
    }

    // no desire to send
    if(!sendbin)
        return true;

    send_in_use = true;

    if(audiosrc)
    {
        if(!addAudioChain(rate))
        {
            delete pd_audiosrc;
            pd_audiosrc = 0;
            delete pd_videosrc;
            pd_videosrc = 0;
            g_object_unref(G_OBJECT(sendbin));
            sendbin = 0;

            error = RtpSessionContext::ErrorGeneric;
            return false;
        }
    }
    if(videosrc)
    {
        if(!addVideoChain())
        {
            delete pd_audiosrc;
            pd_audiosrc = 0;
            delete pd_videosrc;
            pd_videosrc = 0;
            g_object_unref(G_OBJECT(sendbin));
            sendbin = 0;

            error = RtpSessionContext::ErrorGeneric;
            return false;
        }
    }

    gst_bin_add(GST_BIN(spipeline), sendbin);

    if(!audiosrc && !videosrc)
    {
        // in the case of files, preroll
        gst_element_set_state(spipeline, GST_STATE_PAUSED);
        gst_element_get_state(spipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        //gst_element_set_state(sendbin, GST_STATE_PAUSED);
        //gst_element_get_state(sendbin, NULL, NULL, GST_CLOCK_TIME_NONE);

        /*if(loopFile)
        {
            gst_element_seek(sendPipeline, 1, GST_FORMAT_TIME,
                (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
                GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, 0);
        }*/
    }
    else
    {
        // in the case of live transmission, wait for it to start and signal
        //gst_element_set_state(sendbin, GST_STATE_READY);
        //gst_element_get_state(sendbin, NULL, NULL, GST_CLOCK_TIME_NONE);

#ifdef RTPWORKER_DEBUG
        qDebug("changing state...\n");
#endif

        //gst_element_set_state(sendbin, GST_STATE_PLAYING);
        if(audiosrc)
        {
            gst_element_link(audiosrc, sendbin);
            //pd_audiosrc->activate();
        }
        if(videosrc)
        {
            gst_element_link(videosrc, sendbin);
            //pd_videosrc->activate();
        }
#ifdef RTPWORKER_DEBUG
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(spipeline), GST_DEBUG_GRAPH_SHOW_ALL, "psimedia_send_inactive");
#endif

        /*if(shared_clock && recv_clock_is_shared)
        {
            qDebug("send pipeline slaving to recv clock\n");
            gst_pipeline_use_clock(GST_PIPELINE(spipeline), shared_clock);
        }*/

        //gst_element_set_state(pipeline, GST_STATE_PLAYING);
        //gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        send_pipelineContext->activate();

        // 10 seconds ought to be enough time to init (video devices probing may take considerable time)
        int ret = gst_element_get_state(spipeline, NULL, NULL, 10 * GST_SECOND);
        //gst_element_get_state(sendbin, NULL, NULL, GST_CLOCK_TIME_NONE);
        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_NO_PREROLL)
        {
#ifdef RTPWORKER_DEBUG
            qDebug("error/timeout while setting send pipeline to PLAYING\n");
#endif
            cleanup();
            error = RtpSessionContext::ErrorGeneric;
            return false;
        }

        if(!shared_clock && use_shared_clock)
        {
            qDebug("send clock is master\n");

            shared_clock = gst_pipeline_get_clock(GST_PIPELINE(spipeline));
            gst_pipeline_use_clock(GST_PIPELINE(spipeline), shared_clock);
            send_clock_is_shared = true;

            // if recv active, apply this clock to it
            if(recv_in_use)
            {
                qDebug("recv pipeline slaving to send clock\n");
                gst_element_set_state(rpipeline, GST_STATE_READY);
                gst_element_get_state(rpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
                gst_pipeline_use_clock(GST_PIPELINE(rpipeline), shared_clock);
                gst_element_set_state(rpipeline, GST_STATE_PLAYING);
            }
        }

#ifdef RTPWORKER_DEBUG
        qDebug("state changed\n");

        qDebug("Dumping send pipeline");
        dump_pipeline(spipeline);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(spipeline), GST_DEBUG_GRAPH_SHOW_ALL, "psimedia_send_active");

#endif

        if(!getCaps())
        {
            error = RtpSessionContext::ErrorCodec;
            return false;
        }

        actual_localAudioPayloadInfo = localAudioPayloadInfo;
        actual_localVideoPayloadInfo = localVideoPayloadInfo;
    }

    return true;
}

bool RtpWorker::startRecv()
{
    QString acodec, vcodec;
    GstElement *audioout = 0;
    GstElement *asrc = 0;

    // TODO: support more than opus
    int opus_at = -1;
    int samplerate = -1;
    for(int n = 0; n < remoteAudioPayloadInfo.count(); ++n)
    {
        const PPayloadInfo &ri = remoteAudioPayloadInfo[n];
        if(ri.name.toUpper() == "OPUS")
        {
            if (ri.clockrate > samplerate) {
                opus_at = n;
                samplerate = ri.clockrate;
            }
        }
    }
    if (samplerate != 16000) {
        cleanup();
        startSend(samplerate);
    }

    // TODO: support more than theora
    int theora_at = -1;
    for(int n = 0; n < remoteVideoPayloadInfo.count(); ++n)
    {
        const PPayloadInfo &ri = remoteVideoPayloadInfo[n];
        if(ri.name.toUpper() == "THEORA" && ri.clockrate == 90000)
        {
            theora_at = n;
            break;
        }
    }

    // if remote does not support our codecs, error out
    // FIXME: again, support more than opus/theora
    if((!remoteAudioPayloadInfo.isEmpty() && opus_at == -1) ||
            (!remoteVideoPayloadInfo.isEmpty() && theora_at == -1))
    {
        return false;
    }

    if(!remoteAudioPayloadInfo.isEmpty() && opus_at != -1)
    {
#ifdef RTPWORKER_DEBUG
        qDebug("setting up audio recv\n");
#endif

        int at = opus_at;

        GstStructure *cs = payloadInfoToStructure(remoteAudioPayloadInfo[at], "audio");
        if(!cs)
        {
#ifdef RTPWORKER_DEBUG
            qDebug("cannot parse payload info\n");
#endif
            return false;
        }

        if(recv_in_use)
            return false;

        if(!recvbin)
            recvbin = gst_bin_new("recvbin");

        audiortpsrc_mutex.lock();
        audiortpsrc = gst_element_factory_make("appsrc", NULL);
        audiortpsrc_mutex.unlock();

        GstCaps *caps = gst_caps_new_empty();
        gst_caps_append_structure(caps, cs);
        g_object_set(G_OBJECT(audiortpsrc), "caps", caps, NULL);
        gst_caps_unref(caps);

        // FIXME: what if we don't have a name and just id?
        //   it's okay, for now we only support opus which requires
        //   the name..
        // UPD 2016-04-16: it's not clear after migrating to opus
        acodec = remoteAudioPayloadInfo[at].name.toLower();
    }

    if(!remoteVideoPayloadInfo.isEmpty() && theora_at != -1)
    {
#ifdef RTPWORKER_DEBUG
        qDebug("setting up video recv\n");
#endif

        int at = theora_at;

        GstStructure *cs = payloadInfoToStructure(remoteVideoPayloadInfo[at], "video");
        if(!cs)
        {
#ifdef RTPWORKER_DEBUG
            qDebug("cannot parse payload info\n");
#endif
            goto fail1;
        }

        if(recv_in_use)
            return false;

        if(!recvbin)
            recvbin = gst_bin_new("recvbin");

        videortpsrc_mutex.lock();
        videortpsrc = gst_element_factory_make("appsrc", NULL);
        videortpsrc_mutex.unlock();

        GstCaps *caps = gst_caps_new_empty();
        gst_caps_append_structure(caps, cs);
        g_object_set(G_OBJECT(videortpsrc), "caps", caps, NULL);
        gst_caps_unref(caps);

        // FIXME: what if we don't have a name and just id?
        //   it's okay, for now we only really support theora which
        //   requires the name..
        vcodec = remoteVideoPayloadInfo[at].name;
        if(vcodec == "H263-1998") // FIXME: gross
            vcodec = "h263p";
        else
            vcodec = vcodec.toLower();
    }

    // no desire to receive
    if(!recvbin)
        return true;

    recv_in_use = true;

    if(audiortpsrc)
    {
        GstElement *audiodec = bins_audiodec_create(acodec);
        if(!audiodec)
            goto fail1;

        if(!aout.isEmpty())
        {
#ifdef RTPWORKER_DEBUG
            qDebug("creating audioout\n");
#endif

            pd_audiosink = PipelineDeviceContext::create(recv_pipelineContext, aout, PDevice::AudioOut);
            if(!pd_audiosink)
            {
#ifdef RTPWORKER_DEBUG
                qDebug("failed to create audio output element\n");
#endif
                goto fail1;
            }
            if (pd_audiosrc) {
                PipelineDeviceOptions opts = pd_audiosrc->options();
                opts.aec = true;
                opts.echoProberName = pd_audiosink->options().echoProberName;
                pd_audiosrc->setOptions(opts);
            }

            audioout = pd_audiosink->element();
        }
        else
            audioout = gst_element_factory_make("fakesink", NULL);

        {
            QMutexLocker locker(&volumeout_mutex);
            volumeout = gst_element_factory_make("volume", NULL);
            double vol = (double)outputVolume / 100;
            g_object_set(G_OBJECT(volumeout), "volume", vol, NULL);
        }

        GstElement *audioconvert = gst_element_factory_make("audioconvert", NULL);
        GstElement *audioresample = gst_element_factory_make("audioresample", NULL);
        if(pd_audiosink)
            asrc = audioresample;

        gst_bin_add(GST_BIN(recvbin), audiortpsrc);
        gst_bin_add(GST_BIN(recvbin), audiodec);
        gst_bin_add(GST_BIN(recvbin), volumeout);
        gst_bin_add(GST_BIN(recvbin), audioconvert);
        gst_bin_add(GST_BIN(recvbin), audioresample);
        if(!asrc)
            gst_bin_add(GST_BIN(recvbin), audioout);

        gst_element_link_many(audiortpsrc, audiodec, volumeout, audioconvert, audioresample, NULL);
        if(!asrc)
            gst_element_link(audioresample, audioout);

        actual_remoteAudioPayloadInfo = remoteAudioPayloadInfo;
    }

    if(videortpsrc)
    {
        GstElement *videodec = bins_videodec_create(vcodec);
        if(!videodec)
            goto fail1;

        GstElement *videoconvert = gst_element_factory_make("videoconvert", NULL);
        GstAppSink *appVideoSink = makeVideoPlayAppSink("netviedeoplay");

        GstAppSinkCallbacks sinkVideoCb;
        sinkVideoCb.new_sample = cb_show_frame_output;
        sinkVideoCb.eos = cb_packet_ready_eos_stub; // TODO
        sinkVideoCb.new_preroll = cb_packet_ready_preroll_stub; // TODO
        gst_app_sink_set_callbacks(appVideoSink, &sinkVideoCb, this, NULL);

        gst_bin_add(GST_BIN(recvbin), videortpsrc);
        gst_bin_add(GST_BIN(recvbin), videodec);
        gst_bin_add(GST_BIN(recvbin), videoconvert);
        gst_bin_add(GST_BIN(recvbin), (GstElement *)appVideoSink);

        gst_element_link_many(videortpsrc, videodec, videoconvert, (GstElement *)appVideoSink, NULL);

        actual_remoteVideoPayloadInfo = remoteVideoPayloadInfo;
    }

    //gst_element_set_locked_state(recvbin, TRUE);
    gst_bin_add(GST_BIN(rpipeline), recvbin);

    if(asrc)
    {
        GstPad *pad = gst_element_get_static_pad(asrc, "src");
        gst_element_add_pad(recvbin, gst_ghost_pad_new_from_template("src", pad,
                                                                     gst_static_pad_template_get(&raw_audio_src_template)));
        gst_object_unref(GST_OBJECT(pad));

        gst_element_link(recvbin, audioout);
    }

    if(shared_clock && send_clock_is_shared)
    {
        qDebug("recv pipeline slaving to send clock\n");
        gst_pipeline_use_clock(GST_PIPELINE(rpipeline), shared_clock);
    }

    //gst_element_set_locked_state(recvbin, FALSE);
    //gst_element_set_state(recvbin, GST_STATE_PLAYING);
#ifdef RTPWORKER_DEBUG
    qDebug("activating\n");
#endif

    gst_element_set_state(rpipeline, GST_STATE_READY);
    gst_element_get_state(rpipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

    recv_pipelineContext->activate();

    /*if(!shared_clock && use_shared_clock)
    {
        qDebug("recv clock is master\n");

        shared_clock = gst_pipeline_get_clock(GST_PIPELINE(rpipeline));
        gst_pipeline_use_clock(GST_PIPELINE(rpipeline), shared_clock);
        recv_clock_is_shared = true;
    }*/

#ifdef RTPWORKER_DEBUG
    qDebug("receive pipeline started\n");
#endif
    return true;

fail1:
    audiortpsrc_mutex.lock();
    if(audiortpsrc)
    {
        g_object_unref(G_OBJECT(audiortpsrc));
        audiortpsrc = 0;
    }
    audiortpsrc_mutex.unlock();

    videortpsrc_mutex.lock();
    if(videortpsrc)
    {
        g_object_unref(G_OBJECT(videortpsrc));
        videortpsrc = 0;
    }
    videortpsrc_mutex.unlock();

    if(recvbin)
    {
        g_object_unref(G_OBJECT(recvbin));
        recvbin = 0;
    }

    delete pd_audiosink;
    pd_audiosink = 0;

    recv_in_use = false;

    return false;
}

bool RtpWorker::addAudioChain()
{
    return addAudioChain(16000);
}

bool RtpWorker::addAudioChain(int rate)
{
    // TODO: support other codecs.  for now, we only support opus 16khz
    QString codec = "opus";
    int size = 16;
    int channels = 1;
    //QString codec = localAudioParams[0].codec;
    //int rate = localAudioParams[0].sampleRate;
    //int size = localAudioParams[0].sampleSize;
    //int channels = localAudioParams[0].channels;
#ifdef RTPWORKER_DEBUG
    qDebug("codec=%s\n", qPrintable(codec));
#endif

    // see if we need to match a pt id
    int pt = -1;
    for(int n = 0; n < remoteAudioPayloadInfo.count(); ++n)
    {
        const PPayloadInfo &ri = remoteAudioPayloadInfo[n];
        if(ri.name.toUpper() == "OPUS" && ri.clockrate == rate)
        {
            pt = ri.id;
            break;
        }
    }

    // NOTE: we don't bother with a maxbitrate constraint on audio yet

    GstElement *audioenc = bins_audioenc_create(codec, pt, rate, size, channels);
    if(!audioenc)
        return false;

    {
        QMutexLocker locker(&volumein_mutex);
        volumein = gst_element_factory_make("volume", NULL);
        double vol = (double)inputVolume / 100;
        g_object_set(G_OBJECT(volumein), "volume", vol, NULL);
    }

    GstElement *audiortpsink = gst_element_factory_make("appsink", NULL);
    GstAppSink *appRtpSink = reinterpret_cast<GstAppSink *>(audiortpsink);

    if(!fileDemux)
        g_object_set(G_OBJECT(appRtpSink), "sync", FALSE, NULL);

    GstAppSinkCallbacks sinkCb;
    sinkCb.new_sample = cb_packet_ready_rtp_audio;
    sinkCb.eos = cb_packet_ready_eos_stub; // TODO
    sinkCb.new_preroll = cb_packet_ready_preroll_stub; // TODO
    gst_app_sink_set_callbacks(appRtpSink, &sinkCb, this, NULL);

    GstElement *queue = 0;
    if(fileDemux)
        queue = gst_element_factory_make("queue", NULL);

    if(queue)
        gst_bin_add(GST_BIN(sendbin), queue);

    gst_bin_add(GST_BIN(sendbin), volumein);
    gst_bin_add(GST_BIN(sendbin), audioenc);
    gst_bin_add(GST_BIN(sendbin), audiortpsink);

    gst_element_link_many(volumein, audioenc, audiortpsink, NULL);

    audiortppay = audioenc;

    if(fileDemux)
    {
        gst_element_link(queue, volumein);

        gst_element_set_state(queue, GST_STATE_PAUSED);
        gst_element_set_state(volumein, GST_STATE_PAUSED);
        gst_element_set_state(audioenc, GST_STATE_PAUSED);
        gst_element_set_state(audiortpsink, GST_STATE_PAUSED);

        gst_element_link(audiosrc, queue);
    }
    else
    {
        GstPad *pad = gst_element_get_static_pad(volumein, "sink");
        gst_element_add_pad(sendbin, gst_ghost_pad_new_from_template("sink0", pad,
                                                                     gst_static_pad_template_get(&raw_audio_sink_template)));
        gst_object_unref(GST_OBJECT(pad));
    }

    return true;
}
#define VIDEO_PREP

bool RtpWorker::addVideoChain()
{
    // TODO: support other codecs.  for now, we only support theora
    QString codec = "theora";
    QSize size = QSize(640, 480);
    int fps = 30;
    //QString codec = localVideoParams[0].codec;
    //QSize size = localVideoParams[0].size;
    //int fps = localVideoParams[0].fps;
#ifdef RTPWORKER_DEBUG
    qDebug("codec=%s\n", qPrintable(codec));
#endif

    // see if we need to match a pt id
    int pt = -1;
    for(int n = 0; n < remoteVideoPayloadInfo.count(); ++n)
    {
        const PPayloadInfo &ri = remoteVideoPayloadInfo[n];
        if(ri.name.toUpper() == "THEORA" && ri.clockrate == 90000)
        {
            pt = ri.id;
            break;
        }
    }

    int videokbps = maxbitrate;
    // NOTE: we assume audio takes 45kbps
    if(audiortppay)
        videokbps -= 45;

#ifdef VIDEO_PREP
    GstElement *videoprep = bins_videoprep_create(size, fps, fileDemux ? false : true);
    if(!videoprep)
        return false;
#endif
    GstElement *videoenc = bins_videoenc_create(codec, pt, videokbps);
    if(!videoenc)
    {
#ifdef VIDEO_PREP
        g_object_unref(G_OBJECT(videoprep));
#endif
        return false;
    }

    GstElement *videotee = gst_element_factory_make("tee", NULL);

    GstElement *playqueue = gst_element_factory_make("queue", NULL);
    GstElement *videoconvertplay = gst_element_factory_make("videoconvert", NULL);
    GstAppSink *appVideoSink = makeVideoPlayAppSink("sourcevideoplay");


    GstAppSinkCallbacks sinkPreviewCb;
    sinkPreviewCb.new_sample = cb_show_frame_preview;
    sinkPreviewCb.eos = cb_packet_ready_eos_stub; // TODO
    sinkPreviewCb.new_preroll = cb_packet_ready_preroll_stub; // TODO
    gst_app_sink_set_callbacks(appVideoSink, &sinkPreviewCb, this, NULL);


    GstElement *rtpqueue = gst_element_factory_make("queue", NULL);
    GstElement *videortpsink = gst_element_factory_make("appsink", NULL); // was apprtpsink
    GstAppSink *appRtpSink = reinterpret_cast<GstAppSink *>(videortpsink);
    if(!fileDemux)
        g_object_set(G_OBJECT(appRtpSink), "sync", FALSE, NULL);

    GstAppSinkCallbacks sinkCb;
    sinkCb.new_sample = cb_packet_ready_rtp_video;
    sinkCb.eos = cb_packet_ready_eos_stub; // TODO
    sinkCb.new_preroll = cb_packet_ready_preroll_stub; // TODO
    gst_app_sink_set_callbacks(appRtpSink, &sinkCb, this, NULL);


    GstElement *queue = 0;
    if(fileDemux)
        queue = gst_element_factory_make("queue", NULL);

    if(queue)
        gst_bin_add(GST_BIN(sendbin), queue);
#ifdef VIDEO_PREP
    gst_bin_add(GST_BIN(sendbin), videoprep);
#endif
    gst_bin_add(GST_BIN(sendbin), videotee);
    gst_bin_add(GST_BIN(sendbin), playqueue);
    gst_bin_add(GST_BIN(sendbin), videoconvertplay);
    gst_bin_add(GST_BIN(sendbin), (GstElement *)appVideoSink);
    gst_bin_add(GST_BIN(sendbin), rtpqueue);
    gst_bin_add(GST_BIN(sendbin), videoenc);
    gst_bin_add(GST_BIN(sendbin), videortpsink);
#ifdef VIDEO_PREP
    gst_element_link(videoprep, videotee);
#endif
    gst_element_link_many(videotee, playqueue, videoconvertplay, (GstElement *)appVideoSink, NULL);
    gst_element_link_many(videotee, rtpqueue, videoenc, videortpsink, NULL); // FIXME!

    videortppay = videoenc;

    if(fileDemux)
    {
#ifdef VIDEO_PREP
        gst_element_link(queue, videoprep);
#else
        gst_element_link(queue, videotee);
#endif

        gst_element_set_state(queue, GST_STATE_PAUSED);
#ifdef VIDEO_PREP
        gst_element_set_state(videoprep, GST_STATE_PAUSED);
#endif
        gst_element_set_state(videotee, GST_STATE_PAUSED);
        gst_element_set_state(playqueue, GST_STATE_PAUSED);
        gst_element_set_state(videoconvertplay, GST_STATE_PAUSED);
        gst_element_set_state((GstElement *)appVideoSink, GST_STATE_PAUSED);
        gst_element_set_state(rtpqueue, GST_STATE_PAUSED);
        gst_element_set_state(videoenc, GST_STATE_PAUSED);
        gst_element_set_state(videortpsink, GST_STATE_PAUSED);

        gst_element_link(videosrc, queue);
    }
    else
    {
#ifdef VIDEO_PREP
        GstPad *pad = gst_element_get_static_pad(videoprep, "sink");
#else
        GstPad *pad = gst_element_get_static_pad(videotee, "sink");
#endif
        gst_element_add_pad(sendbin, gst_ghost_pad_new_from_template("sink1", pad,
                                                                     gst_static_pad_template_get(&raw_video_sink_template)));
        gst_object_unref(GST_OBJECT(pad));
    }

    return true;
}

bool RtpWorker::getCaps()
{
    if(audiortppay)
    {
        GstPad *pad = gst_element_get_static_pad(audiortppay, "src");
        GstCaps *caps = gst_pad_get_current_caps(pad);
        if(!caps)
        {
#ifdef RTPWORKER_DEBUG
            qDebug("can't get audio caps\n");
#endif
            return false;
        }

#ifdef RTPWORKER_DEBUG
        gchar *gstr = gst_caps_to_string(caps);
        QString capsString = QString::fromUtf8(gstr);
        g_free(gstr);
        qDebug("rtppay caps audio: [%s]\n", qPrintable(capsString));
#endif

        gst_object_unref(pad);

        GstStructure *cs = gst_caps_get_structure(caps, 0);

        PPayloadInfo pi = structureToPayloadInfo(cs);
        if(pi.id == -1)
        {
            gst_caps_unref(caps);
            return false;
        }

        gst_caps_unref(caps);

        PPayloadInfo opusnb;
        opusnb.id = 97;
        opusnb.name = "OPUS";
        opusnb.clockrate = 8000;
        opusnb.channels = 1;
        opusnb.ptime = pi.ptime;
        opusnb.maxptime = pi.maxptime;

        QList<PPayloadInfo> ppil;
        ppil << pi;
        ppil << opusnb;


        localAudioPayloadInfo = ppil;
        canTransmitAudio = true;
    }

    if(videortppay)
    {
        GstPad *pad = gst_element_get_static_pad(videortppay, "src");
        GstCaps *caps = gst_pad_get_current_caps(pad);
        if(!caps)
        {
#ifdef RTPWORKER_DEBUG
            qWarning("can't get video caps\n");
#endif
            return false;
        }

#ifdef RTPWORKER_DEBUG
        gchar *gstr = gst_caps_to_string(caps);
        QString capsString = QString::fromUtf8(gstr);
        g_free(gstr);
        qDebug("rtppay caps video: [%s]\n", qPrintable(capsString));
#endif

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

    return true;
}

bool RtpWorker::updateTheoraConfig()
{
    // first, are we using theora currently?
    int theora_at = -1;
    for(int n = 0; n < actual_remoteVideoPayloadInfo.count(); ++n)
    {
        const PPayloadInfo &ri = actual_remoteVideoPayloadInfo[n];
        if(ri.name.toUpper() == "THEORA" && ri.clockrate == 90000)
        {
            theora_at = n;
            break;
        }
    }
    if(theora_at == -1)
        return false;

    // if so, update the videortpsrc caps
    for(int n = 0; n < remoteAudioPayloadInfo.count(); ++n)
    {
        const PPayloadInfo &ri = remoteVideoPayloadInfo[n];
        if(ri.name.toUpper() == "THEORA" && ri.clockrate == 90000 && ri.id == actual_remoteVideoPayloadInfo[theora_at].id)
        {
            GstStructure *cs = payloadInfoToStructure(remoteVideoPayloadInfo[n], "video");
            if(!cs)
            {
#ifdef RTPWORKER_DEBUG
                qDebug("cannot parse payload info\n");
#endif
                continue;
            }

            QMutexLocker locker(&videortpsrc_mutex);
            if(!videortpsrc)
                continue;

            GstCaps *caps = gst_caps_new_empty();

            gst_caps_append_structure(caps, cs);
            g_object_set(G_OBJECT(videortpsrc), "caps", caps, NULL);
            gst_caps_unref(caps);

            actual_remoteAudioPayloadInfo[theora_at] = ri;
            return true;
        }
    }

    return false;
}

RtpWorker::Frame RtpWorker::Frame::pullFromSink(GstAppSink *appsink)
{
    Frame frame;
    int width, height;
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);

    /*
    gchar *capsstr;
    capsstr = gst_caps_to_string(caps);
    qDebug("recv video frame caps: %s\n", capsstr);
    g_free (capsstr);
*/

    GstStructure *capsStruct = gst_caps_get_structure(caps,0);
    gst_structure_get_int(capsStruct,"width",&width);
    gst_structure_get_int(capsStruct,"height",&height);

    if ((gsize)(width * height * 4) == gst_buffer_get_size(buffer)) {
        QImage image(width, height, QImage::Format_RGB32);
#if QT_VERSION >= QT_VERSION_CHECK(5,11,0)
        gst_buffer_extract(buffer, 0, image.bits(), image.sizeInBytes());
#else
        gst_buffer_extract(buffer, 0, image.bits(), image.byteCount());
#endif
        frame.image = image;
    } else {
        qDebug("wrong size of received buffer: %x != %lx", (width * height * 4), gst_buffer_get_size(buffer));
        gchar *capsstr;
        capsstr = gst_caps_to_string(caps);
        qDebug("recv video frame caps: %s\n", capsstr);
        g_free (capsstr);
    }
    gst_sample_unref(sample);

    return frame;
}

}
