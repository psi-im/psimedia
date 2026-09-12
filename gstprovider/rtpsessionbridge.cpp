/*
 * Copyright (C) 2026  Psi IM team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "rtpsessionbridge.h"

#include "payloadinfo.h"

#include <QMutexLocker>

#include <cstring>

namespace PsiMedia {
namespace {

using CapsMap = QHash<int, GstCaps *>;

GstPad *requestPad(GstElement *element, const char *name)
{
#if GST_CHECK_VERSION(1, 20, 0)
    return gst_element_request_pad_simple(element, name);
#else
    return gst_element_get_request_pad(element, name);
#endif
}

bool linkSourceToPad(GstElement *source, GstPad *sinkPad)
{
    GstPad *sourcePad = gst_element_get_static_pad(source, "src");
    if (!sourcePad)
        return false;
    const bool linked = gst_pad_link(sourcePad, sinkPad) == GST_PAD_LINK_OK;
    gst_object_unref(sourcePad);
    return linked;
}

bool linkPadToSink(GstPad *sourcePad, GstElement *sink)
{
    GstPad *sinkPad = gst_element_get_static_pad(sink, "sink");
    if (!sinkPad)
        return false;
    const bool linked = gst_pad_link(sourcePad, sinkPad) == GST_PAD_LINK_OK;
    gst_object_unref(sinkPad);
    return linked;
}

void configureAppSrc(GstAppSrc *source, const char *mediaType, bool timestamp)
{
    GstCaps *caps = gst_caps_new_empty_simple(mediaType);
    gst_app_src_set_caps(source, caps);
    gst_caps_unref(caps);
    g_object_set(G_OBJECT(source), "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", timestamp, nullptr);
}

void configureAppSink(GstAppSink *sink)
{
    g_object_set(G_OBJECT(sink), "sync", FALSE, "async", FALSE, nullptr);
}

void unrefElement(GstElement *element)
{
    if (element)
        gst_object_unref(element);
}

void unrefCapsMap(CapsMap &capsMap)
{
    for (auto caps : std::as_const(capsMap))
        gst_caps_unref(caps);
    capsMap.clear();
}

GstCaps *capsForPayload(const PPayloadInfo &payload, const QString &media)
{
    GstStructure *structure = payloadInfoToStructure(payload, media);
    if (!structure)
        return nullptr;
    GstCaps *caps = gst_caps_new_empty();
    gst_caps_append_structure(caps, structure);
    return caps;
}

bool payloadsCompatible(const PPayloadInfo &local, const PPayloadInfo &remote)
{
    if (!local.name.isEmpty() && !remote.name.isEmpty()
        && local.name.compare(remote.name, Qt::CaseInsensitive) != 0)
        return false;
    if (local.clockrate >= 0 && remote.clockrate >= 0 && local.clockrate != remote.clockrate)
        return false;
    if (local.channels >= 0 && remote.channels >= 0 && local.channels != remote.channels)
        return false;
    return true;
}

PPayloadInfo commonPayload(const PPayloadInfo &primary, const PPayloadInfo *secondary)
{
    PPayloadInfo common = primary;
    if (secondary) {
        if (common.name.isEmpty())
            common.name = secondary->name;
        if (common.clockrate < 0)
            common.clockrate = secondary->clockrate;
        if (common.channels < 0)
            common.channels = secondary->channels;
    }
    common.ptime = -1;
    common.maxptime = -1;
    common.parameters.clear();
    return common;
}

} // namespace

RtpSessionBridge::RtpSessionBridge(QString media) : media_(std::move(media)) { build(); }

RtpSessionBridge::~RtpSessionBridge() { cleanup(); }

bool RtpSessionBridge::build()
{
    GstElement *pipeline       = gst_pipeline_new(nullptr);
    GstElement *session        = gst_element_factory_make("rtpsession", nullptr);
    GstElement *sendRtpInput   = gst_element_factory_make("appsrc", nullptr);
    GstElement *recvRtpInput   = gst_element_factory_make("appsrc", nullptr);
    GstElement *recvRtcpInput  = gst_element_factory_make("appsrc", nullptr);
    GstElement *sendRtpOutput  = gst_element_factory_make("appsink", nullptr);
    GstElement *recvRtpOutput  = gst_element_factory_make("appsink", nullptr);
    GstElement *sendRtcpOutput = gst_element_factory_make("appsink", nullptr);

    if (!pipeline || !session || !sendRtpInput || !recvRtpInput || !recvRtcpInput || !sendRtpOutput
        || !recvRtpOutput || !sendRtcpOutput) {
        // None of these elements has been parented yet. Release every floating
        // object explicitly, including rtpsession itself and the pipeline.
        unrefElement(sendRtpInput);
        unrefElement(recvRtpInput);
        unrefElement(recvRtcpInput);
        unrefElement(sendRtpOutput);
        unrefElement(recvRtpOutput);
        unrefElement(sendRtcpOutput);
        unrefElement(session);
        unrefElement(pipeline);
        return false;
    }

    configureAppSrc(GST_APP_SRC(sendRtpInput), "application/x-rtp", false);
    configureAppSrc(GST_APP_SRC(recvRtpInput), "application/x-rtp", true);
    configureAppSrc(GST_APP_SRC(recvRtcpInput), "application/x-rtcp", true);
    configureAppSink(GST_APP_SINK(sendRtpOutput));
    configureAppSink(GST_APP_SINK(recvRtpOutput));
    configureAppSink(GST_APP_SINK(sendRtcpOutput));

    // gst_bin_add_many() sinks the elements' floating references. From here on
    // the pipeline owns all children; member element pointers are borrowed and
    // cleanup() releases the pipeline after releasing our request-pad refs.
    gst_bin_add_many(GST_BIN(pipeline), session, sendRtpInput, recvRtpInput, recvRtcpInput, sendRtpOutput,
                     recvRtpOutput, sendRtcpOutput, nullptr);

    pipeline_       = pipeline;
    session_        = session;
    sendRtpInput_   = GST_APP_SRC(sendRtpInput);
    recvRtpInput_   = GST_APP_SRC(recvRtpInput);
    recvRtcpInput_  = GST_APP_SRC(recvRtcpInput);
    sendRtpOutput_  = GST_APP_SINK(sendRtpOutput);
    recvRtpOutput_  = GST_APP_SINK(recvRtpOutput);
    sendRtcpOutput_ = GST_APP_SINK(sendRtcpOutput);

    const auto fail = [this]() {
        cleanup();
        return false;
    };

    // Feedback packets such as NACK/PLI belong to the media session. Encryption,
    // RTCP mux and BUNDLE are deliberately outside this bridge.
    gst_util_set_object_arg(G_OBJECT(session_), "rtp-profile", "avpf");

    sendRtpSinkPad_  = requestPad(session_, "send_rtp_sink");
    recvRtpSinkPad_  = requestPad(session_, "recv_rtp_sink");
    recvRtcpSinkPad_ = requestPad(session_, "recv_rtcp_sink");
    sendRtcpSrcPad_  = requestPad(session_, "send_rtcp_src");
    if (!sendRtpSinkPad_ || !recvRtpSinkPad_ || !recvRtcpSinkPad_ || !sendRtcpSrcPad_)
        return fail();

    if (!linkSourceToPad(sendRtpInput, sendRtpSinkPad_) || !linkSourceToPad(recvRtpInput, recvRtpSinkPad_)
        || !linkSourceToPad(recvRtcpInput, recvRtcpSinkPad_))
        return fail();

    {
        GstPad *pad = gst_element_get_static_pad(session_, "send_rtp_src");
        if (!pad)
            return fail();
        const bool linked = linkPadToSink(pad, sendRtpOutput);
        gst_object_unref(pad);
        if (!linked)
            return fail();
    }
    {
        GstPad *pad = gst_element_get_static_pad(session_, "recv_rtp_src");
        if (!pad)
            return fail();
        const bool linked = linkPadToSink(pad, recvRtpOutput);
        gst_object_unref(pad);
        if (!linked)
            return fail();
    }
    if (!linkPadToSink(sendRtcpSrcPad_, sendRtcpOutput))
        return fail();

    g_signal_connect(session_, "request-pt-map", G_CALLBACK(requestPtMap), this);

    GstAppSinkCallbacks sendRtpCallbacks {};
    sendRtpCallbacks.new_sample = sendRtpReady;
    gst_app_sink_set_callbacks(sendRtpOutput_, &sendRtpCallbacks, this, nullptr);

    GstAppSinkCallbacks recvRtpCallbacks {};
    recvRtpCallbacks.new_sample = recvRtpReady;
    gst_app_sink_set_callbacks(recvRtpOutput_, &recvRtpCallbacks, this, nullptr);

    GstAppSinkCallbacks sendRtcpCallbacks {};
    sendRtcpCallbacks.new_sample = sendRtcpReady;
    gst_app_sink_set_callbacks(sendRtcpOutput_, &sendRtcpCallbacks, this, nullptr);

    GObject *internalSession = nullptr;
    g_object_get(session_, "internal-session", &internalSession, nullptr);
    if (!internalSession)
        return fail();
    g_signal_connect(internalSession, "on-receiving-rtcp", G_CALLBACK(receivingRtcp), this);
    g_object_unref(internalSession);

    return true;
}

void RtpSessionBridge::cleanup()
{
    running_ = false;
    if (pipeline_)
        gst_element_set_state(pipeline_, GST_STATE_NULL);

    if (session_) {
        if (sendRtpSinkPad_) {
            gst_element_release_request_pad(session_, sendRtpSinkPad_);
            gst_object_unref(sendRtpSinkPad_);
        }
        if (recvRtpSinkPad_) {
            gst_element_release_request_pad(session_, recvRtpSinkPad_);
            gst_object_unref(recvRtpSinkPad_);
        }
        if (recvRtcpSinkPad_) {
            gst_element_release_request_pad(session_, recvRtcpSinkPad_);
            gst_object_unref(recvRtcpSinkPad_);
        }
        if (sendRtcpSrcPad_) {
            gst_element_release_request_pad(session_, sendRtcpSrcPad_);
            gst_object_unref(sendRtcpSrcPad_);
        }
    }
    sendRtpSinkPad_ = recvRtpSinkPad_ = recvRtcpSinkPad_ = sendRtcpSrcPad_ = nullptr;

    if (pipeline_)
        gst_object_unref(pipeline_);
    pipeline_       = nullptr;
    session_        = nullptr;
    sendRtpInput_   = nullptr;
    recvRtpInput_   = nullptr;
    recvRtcpInput_  = nullptr;
    sendRtpOutput_  = nullptr;
    recvRtpOutput_  = nullptr;
    sendRtcpOutput_ = nullptr;

    QMutexLocker locker(&payloadMutex_);
    unrefCapsMap(localPayloadCaps_);
    unrefCapsMap(remotePayloadCaps_);
    unrefCapsMap(payloadCaps_);
}

bool RtpSessionBridge::setPayloads(const QList<PPayloadInfo> &local, const QList<PPayloadInfo> &remote)
{
    CapsMap                    nextLocal;
    CapsMap                    nextRemote;
    CapsMap                    nextCommon;
    QHash<int, PPayloadInfo>   localInfo;
    QHash<int, PPayloadInfo>   remoteInfo;

    const auto fail = [&]() {
        unrefCapsMap(nextLocal);
        unrefCapsMap(nextRemote);
        unrefCapsMap(nextCommon);
        return false;
    };

    const auto addDirectional = [&](const PPayloadInfo &payload, CapsMap &capsMap,
                                    QHash<int, PPayloadInfo> &infoMap) {
        if (payload.id < 0 || payload.id > 127)
            return false;

        GstCaps *caps = capsForPayload(payload, media_);
        if (!caps)
            return false;

        const auto existing = capsMap.constFind(payload.id);
        if (existing != capsMap.cend()) {
            const bool same = gst_caps_is_equal(*existing, caps);
            gst_caps_unref(caps);
            return same;
        }

        capsMap.insert(payload.id, caps);
        infoMap.insert(payload.id, payload);
        return true;
    };

    for (const auto &payload : local) {
        if (!addDirectional(payload, nextLocal, localInfo))
            return fail();
    }
    for (const auto &payload : remote) {
        if (!addDirectional(payload, nextRemote, remoteInfo))
            return fail();
    }

    const auto addCommon = [&](const PPayloadInfo &primary, const PPayloadInfo *secondary) {
        if (secondary && !payloadsCompatible(primary, *secondary))
            return false;
        const PPayloadInfo common = commonPayload(primary, secondary);
        GstCaps           *caps   = capsForPayload(common, media_);
        if (!caps)
            return false;
        nextCommon.insert(common.id, caps);
        return true;
    };

    for (auto it = localInfo.cbegin(); it != localInfo.cend(); ++it) {
        const auto remoteIt = remoteInfo.constFind(it.key());
        const auto *peer = remoteIt == remoteInfo.cend() ? nullptr : &remoteIt.value();
        if (!addCommon(it.value(), peer))
            return fail();
    }
    for (auto it = remoteInfo.cbegin(); it != remoteInfo.cend(); ++it) {
        if (localInfo.contains(it.key()))
            continue;
        if (!addCommon(it.value(), nullptr))
            return fail();
    }

    CapsMap oldLocal;
    CapsMap oldRemote;
    CapsMap oldCommon;
    {
        QMutexLocker locker(&payloadMutex_);
        oldLocal.swap(localPayloadCaps_);
        oldRemote.swap(remotePayloadCaps_);
        oldCommon.swap(payloadCaps_);
        localPayloadCaps_.swap(nextLocal);
        remotePayloadCaps_.swap(nextRemote);
        payloadCaps_.swap(nextCommon);
    }

    unrefCapsMap(oldLocal);
    unrefCapsMap(oldRemote);
    unrefCapsMap(oldCommon);

    // clear-pt-map can synchronously invoke request-pt-map, which takes
    // payloadMutex_. Never emit it while holding that mutex.
    if (session_)
        g_signal_emit_by_name(session_, "clear-pt-map");
    return true;
}

bool RtpSessionBridge::start()
{
    if (!pipeline_)
        return false;
    if (running_)
        return true;
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return false;
    running_ = true;
    return true;
}

void RtpSessionBridge::stop()
{
    if (!pipeline_ || !running_)
        return;
    running_ = false;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
}

GstFlowReturn RtpSessionBridge::sendRtp(GstBuffer *buffer)
{
    if (!running_ || !sendRtpInput_ || !buffer)
        return GST_FLOW_FLUSHING;
    return gst_app_src_push_buffer(sendRtpInput_, gst_buffer_ref(buffer));
}

GstFlowReturn RtpSessionBridge::receivePacket(const PRtpPacket &packet)
{
    if (!running_)
        return GST_FLOW_FLUSHING;
    return packet.type == PRtpPacket::Type::Rtp ? pushRaw(recvRtpInput_, packet.rawValue)
                                                : pushRaw(recvRtcpInput_, packet.rawValue);
}

bool RtpSessionBridge::requestRtcp(guint64 maxDelay)
{
    if (!running_ || !session_)
        return false;

    GObject *internalSession = nullptr;
    g_object_get(session_, "internal-session", &internalSession, nullptr);
    if (!internalSession)
        return false;

    gboolean scheduled = FALSE;
    g_signal_emit_by_name(internalSession, "send-rtcp-full", maxDelay, &scheduled);
    g_object_unref(internalSession);
    return scheduled;
}

void RtpSessionBridge::setRtcpMinimumInterval(guint64 interval)
{
    if (session_)
        g_object_set(session_, "rtcp-min-interval", interval, nullptr);
}

GstCaps *RtpSessionBridge::requestPtMap(GstElement *, guint pt, gpointer data)
{
    return static_cast<RtpSessionBridge *>(data)->payloadCaps(pt);
}

GstFlowReturn RtpSessionBridge::sendRtpReady(GstAppSink *sink, gpointer data)
{
    return static_cast<RtpSessionBridge *>(data)->pullNetworkPacket(sink, PRtpPacket::Type::Rtp);
}

GstFlowReturn RtpSessionBridge::recvRtpReady(GstAppSink *sink, gpointer data)
{
    return static_cast<RtpSessionBridge *>(data)->pullMediaPacket(sink);
}

GstFlowReturn RtpSessionBridge::sendRtcpReady(GstAppSink *sink, gpointer data)
{
    return static_cast<RtpSessionBridge *>(data)->pullNetworkPacket(sink, PRtpPacket::Type::Rtcp);
}

void RtpSessionBridge::receivingRtcp(GObject *, GstBuffer *, gpointer data)
{
    static_cast<RtpSessionBridge *>(data)->receivedRtcpPackets_.fetch_add(1);
}

GstCaps *RtpSessionBridge::payloadCaps(guint pt)
{
    QMutexLocker locker(&payloadMutex_);
    auto         it = payloadCaps_.constFind(int(pt));
    return it == payloadCaps_.cend() ? nullptr : gst_caps_ref(*it);
}

GstFlowReturn RtpSessionBridge::pullNetworkPacket(GstAppSink *sink, PRtpPacket::Type type)
{
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_ERROR;
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    const auto size = gst_buffer_get_size(buffer);
    PRtpPacket packet;
    packet.rawValue.resize(int(size));
    packet.type = type;
    if (size && gst_buffer_extract(buffer, 0, packet.rawValue.data(), size) != size) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    gst_sample_unref(sample);

    if (networkPacketHandler_)
        networkPacketHandler_(packet);
    return GST_FLOW_OK;
}

GstFlowReturn RtpSessionBridge::pullMediaPacket(GstAppSink *sink)
{
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_ERROR;
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    if (mediaPacketHandler_)
        mediaPacketHandler_(buffer);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

GstFlowReturn RtpSessionBridge::pushRaw(GstAppSrc *source, const QByteArray &data)
{
    if (!source || data.isEmpty())
        return GST_FLOW_ERROR;
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, gsize(data.size()), nullptr);
    if (!buffer)
        return GST_FLOW_ERROR;
    GstMapInfo info;
    if (!gst_buffer_map(buffer, &info, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    std::memcpy(info.data, data.constData(), size_t(data.size()));
    gst_buffer_unmap(buffer, &info);
    return gst_app_src_push_buffer(source, buffer);
}

} // namespace PsiMedia
