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

} // namespace

RtpSessionBridge::RtpSessionBridge(QString media) : media_(std::move(media)) { build(); }

RtpSessionBridge::~RtpSessionBridge() { cleanup(); }

bool RtpSessionBridge::build()
{
    pipeline_ = gst_pipeline_new(nullptr);
    session_  = gst_element_factory_make("rtpsession", nullptr);
    auto sendRtpInput   = gst_element_factory_make("appsrc", nullptr);
    auto recvRtpInput   = gst_element_factory_make("appsrc", nullptr);
    auto recvRtcpInput  = gst_element_factory_make("appsrc", nullptr);
    auto sendRtpOutput  = gst_element_factory_make("appsink", nullptr);
    auto recvRtpOutput  = gst_element_factory_make("appsink", nullptr);
    auto sendRtcpOutput = gst_element_factory_make("appsink", nullptr);

    if (!pipeline_ || !session_ || !sendRtpInput || !recvRtpInput || !recvRtcpInput || !sendRtpOutput
        || !recvRtpOutput || !sendRtcpOutput) {
        if (sendRtpInput)
            gst_object_unref(sendRtpInput);
        if (recvRtpInput)
            gst_object_unref(recvRtpInput);
        if (recvRtcpInput)
            gst_object_unref(recvRtcpInput);
        if (sendRtpOutput)
            gst_object_unref(sendRtpOutput);
        if (recvRtpOutput)
            gst_object_unref(recvRtpOutput);
        if (sendRtcpOutput)
            gst_object_unref(sendRtcpOutput);
        cleanup();
        return false;
    }

    sendRtpInput_   = GST_APP_SRC(sendRtpInput);
    recvRtpInput_   = GST_APP_SRC(recvRtpInput);
    recvRtcpInput_  = GST_APP_SRC(recvRtcpInput);
    sendRtpOutput_  = GST_APP_SINK(sendRtpOutput);
    recvRtpOutput_  = GST_APP_SINK(recvRtpOutput);
    sendRtcpOutput_ = GST_APP_SINK(sendRtcpOutput);

    configureAppSrc(sendRtpInput_, "application/x-rtp", false);
    configureAppSrc(recvRtpInput_, "application/x-rtp", true);
    configureAppSrc(recvRtcpInput_, "application/x-rtcp", true);
    configureAppSink(sendRtpOutput_);
    configureAppSink(recvRtpOutput_);
    configureAppSink(sendRtcpOutput_);

    gst_bin_add_many(GST_BIN(pipeline_), session_, sendRtpInput, recvRtpInput, recvRtcpInput, sendRtpOutput,
                     recvRtpOutput, sendRtcpOutput, nullptr);

    // Feedback packets such as NACK/PLI belong to the media session. Encryption,
    // RTCP mux and BUNDLE are deliberately outside this bridge.
    gst_util_set_object_arg(G_OBJECT(session_), "rtp-profile", "avpf");

    sendRtpSinkPad_  = requestPad(session_, "send_rtp_sink");
    recvRtpSinkPad_  = requestPad(session_, "recv_rtp_sink");
    recvRtcpSinkPad_ = requestPad(session_, "recv_rtcp_sink");
    sendRtcpSrcPad_  = requestPad(session_, "send_rtcp_src");
    if (!sendRtpSinkPad_ || !recvRtpSinkPad_ || !recvRtcpSinkPad_ || !sendRtcpSrcPad_)
        goto fail;

    if (!linkSourceToPad(sendRtpInput, sendRtpSinkPad_) || !linkSourceToPad(recvRtpInput, recvRtpSinkPad_)
        || !linkSourceToPad(recvRtcpInput, recvRtcpSinkPad_))
        goto fail;

    {
        GstPad *pad = gst_element_get_static_pad(session_, "send_rtp_src");
        if (!pad || !linkPadToSink(pad, sendRtpOutput)) {
            if (pad)
                gst_object_unref(pad);
            goto fail;
        }
        gst_object_unref(pad);
    }
    {
        GstPad *pad = gst_element_get_static_pad(session_, "recv_rtp_src");
        if (!pad || !linkPadToSink(pad, recvRtpOutput)) {
            if (pad)
                gst_object_unref(pad);
            goto fail;
        }
        gst_object_unref(pad);
    }
    if (!linkPadToSink(sendRtcpSrcPad_, sendRtcpOutput))
        goto fail;

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

    {
        GObject *internalSession = nullptr;
        g_object_get(session_, "internal-session", &internalSession, nullptr);
        if (!internalSession)
            goto fail;
        g_signal_connect(internalSession, "on-receiving-rtcp", G_CALLBACK(receivingRtcp), this);
        g_object_unref(internalSession);
    }

    return true;

fail:
    cleanup();
    return false;
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
    for (auto caps : std::as_const(payloadCaps_))
        gst_caps_unref(caps);
    payloadCaps_.clear();
}

bool RtpSessionBridge::setPayloads(const QList<PPayloadInfo> &local, const QList<PPayloadInfo> &remote)
{
    QHash<int, GstCaps *> next;
    auto add = [&](const PPayloadInfo &payload) {
        if (payload.id < 0 || payload.id > 127)
            return false;
        GstStructure *structure = payloadInfoToStructure(payload, media_);
        if (!structure)
            return false;
        GstCaps *caps = gst_caps_new_empty();
        gst_caps_append_structure(caps, structure);
        auto existing = next.constFind(payload.id);
        if (existing != next.cend()) {
            const bool same = gst_caps_is_equal(*existing, caps);
            gst_caps_unref(caps);
            return same;
        }
        next.insert(payload.id, caps);
        return true;
    };

    for (const auto &payload : local) {
        if (!add(payload)) {
            for (auto caps : std::as_const(next))
                gst_caps_unref(caps);
            return false;
        }
    }
    for (const auto &payload : remote) {
        if (!add(payload)) {
            for (auto caps : std::as_const(next))
                gst_caps_unref(caps);
            return false;
        }
    }

    QMutexLocker locker(&payloadMutex_);
    for (auto caps : std::as_const(payloadCaps_))
        gst_caps_unref(caps);
    payloadCaps_ = std::move(next);
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
