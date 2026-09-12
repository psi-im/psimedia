/*
 * Copyright (C) 2026  Psi IM team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef RTPSESSIONBRIDGE_H
#define RTPSESSIONBRIDGE_H

#include "psimediaprovider.h"

#include <QHash>
#include <QMutex>
#include <QString>

#include <atomic>
#include <functional>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

namespace PsiMedia {

/**
 * Shared RFC 3550 session state for one media type.
 *
 * The bridge deliberately has no transport topology. Callers provide and
 * receive semantic RTP/RTCP packets; ICE components, UDP ports, RTCP mux and
 * BUNDLE remain responsibilities of the layer above psimedia.
 */
class RtpSessionBridge {
public:
    using NetworkPacketHandler = std::function<void(const PRtpPacket &)>;
    // The buffer is borrowed for the duration of the callback. A consumer that
    // queues it or passes ownership to appsrc must take its own reference.
    using MediaPacketHandler = std::function<void(GstBuffer *)>;

    explicit RtpSessionBridge(QString media);
    ~RtpSessionBridge();

    RtpSessionBridge(const RtpSessionBridge &)            = delete;
    RtpSessionBridge &operator=(const RtpSessionBridge &) = delete;

    bool isValid() const { return pipeline_ != nullptr; }

    /** Update the negotiated payload map. Safe before or while running. */
    bool setPayloads(const QList<PPayloadInfo> &local, const QList<PPayloadInfo> &remote);

    bool start();
    void stop();

    /** Feed encoded RTP from the sender pipeline, preserving timestamps. */
    GstFlowReturn sendRtp(GstBuffer *buffer);

    /** Feed authenticated RTP or RTCP received from the Jingle layer. */
    GstFlowReturn receivePacket(const PRtpPacket &packet);

    void setNetworkPacketHandler(NetworkPacketHandler handler) { networkPacketHandler_ = std::move(handler); }
    void setMediaPacketHandler(MediaPacketHandler handler) { mediaPacketHandler_ = std::move(handler); }

    /** Request an early RTCP report/feedback packet within maxDelay ns. */
    bool requestRtcp(guint64 maxDelay = 0);

    quint64 receivedRtcpPackets() const { return receivedRtcpPackets_.load(); }

    /** Test/diagnostic tuning; production keeps GStreamer's default interval. */
    void setRtcpMinimumInterval(guint64 interval);

private:
    static GstCaps      *requestPtMap(GstElement *session, guint pt, gpointer data);
    static GstFlowReturn sendRtpReady(GstAppSink *sink, gpointer data);
    static GstFlowReturn recvRtpReady(GstAppSink *sink, gpointer data);
    static GstFlowReturn sendRtcpReady(GstAppSink *sink, gpointer data);
    static void          receivingRtcp(GObject *session, GstBuffer *buffer, gpointer data);

    bool          build();
    void          cleanup();
    GstCaps      *payloadCaps(guint pt);
    GstFlowReturn pullNetworkPacket(GstAppSink *sink, PRtpPacket::Type type);
    GstFlowReturn pullMediaPacket(GstAppSink *sink);
    GstFlowReturn pushRaw(GstAppSrc *source, const QByteArray &data);

    QString media_;

    GstElement *pipeline_        = nullptr;
    GstElement *session_         = nullptr;
    GstAppSrc  *sendRtpInput_    = nullptr;
    GstAppSrc  *recvRtpInput_    = nullptr;
    GstAppSrc  *recvRtcpInput_   = nullptr;
    GstAppSink *sendRtpOutput_   = nullptr;
    GstAppSink *recvRtpOutput_   = nullptr;
    GstAppSink *sendRtcpOutput_  = nullptr;

    GstPad *sendRtpSinkPad_  = nullptr;
    GstPad *recvRtpSinkPad_  = nullptr;
    GstPad *recvRtcpSinkPad_ = nullptr;
    GstPad *sendRtcpSrcPad_  = nullptr;

    QMutex                payloadMutex_;
    QHash<int, GstCaps *> payloadCaps_;
    NetworkPacketHandler  networkPacketHandler_;
    MediaPacketHandler    mediaPacketHandler_;
    std::atomic<quint64>  receivedRtcpPackets_ { 0 };
    bool                  running_ = false;
};

} // namespace PsiMedia

#endif
