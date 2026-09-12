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
#include <QObject>
#include <QQueue>
#include <QString>

#include <atomic>
#include <functional>
#include <utility>
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
 *
 * The construction thread owns the control plane: payload configuration,
 * handler replacement, start/stop, RTCP scheduling and destruction must run
 * there. sendRtp() and receivePacket() may race stop() while the bridge remains
 * alive. GStreamer streaming threads never invoke user handlers directly;
 * packet delivery is queued back to the owner thread.
 */
class RtpSessionBridge : public QObject {
public:
    using NetworkPacketHandler = std::function<void(const PRtpPacket &)>;
    // The buffer is borrowed for the duration of the callback. A consumer that
    // queues it or passes ownership to appsrc must take its own reference.
    using MediaPacketHandler = std::function<void(GstBuffer *)>;

    explicit RtpSessionBridge(QString media);
    ~RtpSessionBridge() override;

    RtpSessionBridge(const RtpSessionBridge &)            = delete;
    RtpSessionBridge &operator=(const RtpSessionBridge &) = delete;

    bool isValid() const { return pipeline_ != nullptr; }

    /** Update the negotiated payload map. Owner thread only; safe while running. */
    bool setPayloads(const QList<PPayloadInfo> &local, const QList<PPayloadInfo> &remote);

    /** Owner-thread control-plane operations. */
    bool start();
    void stop();

    /** Feed encoded RTP from the sender pipeline, preserving timestamps. */
    GstFlowReturn sendRtp(GstBuffer *buffer);

    /** Feed authenticated RTP or RTCP received from the Jingle layer. */
    GstFlowReturn receivePacket(const PRtpPacket &packet);

    /** Handler replacement is serialized on the owner thread. */
    void setNetworkPacketHandler(NetworkPacketHandler handler);
    void setMediaPacketHandler(MediaPacketHandler handler);

    /** Request an early RTCP report/feedback packet within maxDelay ns. Owner thread only. */
    bool requestRtcp(guint64 maxDelay = 0);

    quint64 receivedRtcpPackets() const { return receivedRtcpPackets_.load(); }

    /** Test/diagnostic tuning; owner thread only. */
    void setRtcpMinimumInterval(guint64 interval);

private:
    struct QueuedNetworkPacket {
        quint64    generation = 0;
        PRtpPacket packet;
    };
    struct QueuedMediaPacket {
        quint64   generation = 0;
        GstBuffer *buffer    = nullptr;
    };

    static constexpr int MaxQueuedNetworkPackets = 256;
    static constexpr int MaxQueuedMediaPackets   = 128;

    static GstCaps       *requestPtMap(GstElement *session, guint pt, gpointer data);
    static GstFlowReturn  sendRtpReady(GstAppSink *sink, gpointer data);
    static GstFlowReturn  recvRtpReady(GstAppSink *sink, gpointer data);
    static GstFlowReturn  sendRtcpReady(GstAppSink *sink, gpointer data);
    static void           receivingRtcp(GObject *session, GstBuffer *buffer, gpointer data);

    bool          build();
    void          cleanup();
    bool          ownerThread(const char *operation) const;
    GstCaps      *payloadCaps(guint pt);
    GstFlowReturn pullNetworkPacket(GstAppSink *sink, PRtpPacket::Type type);
    GstFlowReturn pullMediaPacket(GstAppSink *sink);
    GstFlowReturn pushRaw(GstAppSrc *source, const QByteArray &data);

    quint64 deliveryGeneration() const;
    void    enableDeliveries();
    void    disableDeliveries();
    void    clearDeliveryQueuesLocked();
    void    enqueueNetworkPacket(quint64 generation, PRtpPacket packet);
    void    enqueueMediaPacket(quint64 generation, GstBuffer *buffer);
    void    scheduleDeliveryLocked(quint64 generation);
    void    drainDeliveries(quint64 generation);

    QString media_;

    GstElement *pipeline_       = nullptr;
    GstElement *session_        = nullptr;
    GstAppSrc  *sendRtpInput_   = nullptr;
    GstAppSrc  *recvRtpInput_   = nullptr;
    GstAppSrc  *recvRtcpInput_  = nullptr;
    GstAppSink *sendRtpOutput_  = nullptr;
    GstAppSink *recvRtpOutput_  = nullptr;
    GstAppSink *sendRtcpOutput_ = nullptr;

    GstPad *sendRtpSinkPad_  = nullptr;
    GstPad *recvRtpSinkPad_  = nullptr;
    GstPad *recvRtcpSinkPad_ = nullptr;
    GstPad *sendRtcpSrcPad_  = nullptr;

    QMutex                payloadMutex_;
    // Keep the negotiated direction-specific caps intact. rtpsession's
    // request-pt-map callback uses payloadCaps_, a normalized map containing
    // only the codec identity/timing fields common to both directions.
    QHash<int, GstCaps *> localPayloadCaps_;
    QHash<int, GstCaps *> remotePayloadCaps_;
    QHash<int, GstCaps *> payloadCaps_;

    mutable QMutex             deliveryMutex_;
    QQueue<QueuedNetworkPacket> networkQueue_;
    QQueue<QueuedMediaPacket>   mediaQueue_;
    quint64                     generation_                  = 0;
    quint64                     scheduledDeliveryGeneration_ = 0;
    bool                        deliveriesEnabled_           = false;

    NetworkPacketHandler networkPacketHandler_;
    MediaPacketHandler   mediaPacketHandler_;
    std::atomic<quint64> receivedRtcpPackets_ { 0 };
    std::atomic<bool>    running_ { false };
};

} // namespace PsiMedia

#endif
