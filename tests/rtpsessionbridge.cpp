/*
 * Copyright (C) 2026  Psi IM team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "rtpsessionbridge.h"

#include <QCoreApplication>
#include <QDebug>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

QByteArray makeRtp(quint16 sequence, quint32 timestamp, quint32 ssrc)
{
    QByteArray packet(13, '\0');
    auto      *p = reinterpret_cast<uchar *>(packet.data());
    p[0]         = 0x80;
    p[1]         = 111;
    p[2]         = uchar(sequence >> 8);
    p[3]         = uchar(sequence);
    p[4]         = uchar(timestamp >> 24);
    p[5]         = uchar(timestamp >> 16);
    p[6]         = uchar(timestamp >> 8);
    p[7]         = uchar(timestamp);
    p[8]         = uchar(ssrc >> 24);
    p[9]         = uchar(ssrc >> 16);
    p[10]        = uchar(ssrc >> 8);
    p[11]        = uchar(ssrc);
    p[12]        = 0x7f;
    return packet;
}

QByteArray makeReceiverReport(quint32 ssrc)
{
    QByteArray packet(8, '\0');
    auto      *p = reinterpret_cast<uchar *>(packet.data());
    p[0]         = 0x80;
    p[1]         = 201;
    p[2]         = 0;
    p[3]         = 1;
    p[4]         = uchar(ssrc >> 24);
    p[5]         = uchar(ssrc >> 16);
    p[6]         = uchar(ssrc >> 8);
    p[7]         = uchar(ssrc);
    return packet;
}

GstBuffer *bufferFor(const QByteArray &data, GstClockTime pts)
{
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, gsize(data.size()), nullptr);
    if (!buffer)
        return nullptr;
    gst_buffer_fill(buffer, 0, data.constData(), gsize(data.size()));
    GST_BUFFER_PTS(buffer)      = pts;
    GST_BUFFER_DTS(buffer)      = pts;
    GST_BUFFER_DURATION(buffer) = 20 * GST_MSECOND;
    return buffer;
}

bool isRtcp(const QByteArray &packet)
{
    if (packet.size() < 4)
        return false;
    const auto *p = reinterpret_cast<const uchar *>(packet.constData());
    return (p[0] >> 6) == 2 && p[1] >= 192 && p[1] <= 223;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    gst_init(&argc, &argv);

    PsiMedia::PPayloadInfo opus;
    opus.id        = 111;
    opus.name      = QStringLiteral("OPUS");
    opus.clockrate = 48000;
    opus.channels  = 2;

    PsiMedia::RtpSessionBridge bridge(QStringLiteral("audio"));
    if (!bridge.isValid()) {
        qCritical() << "failed to create rtpsession bridge";
        return 1;
    }
    if (!bridge.setPayloads({ opus }, { opus })) {
        qCritical() << "failed to configure PT map";
        return 2;
    }

    std::mutex                         mutex;
    std::condition_variable            changed;
    std::vector<PsiMedia::PRtpPacket> networkPackets;
    int                                receivedMediaPackets = 0;

    bridge.setNetworkPacketHandler([&](const PsiMedia::PRtpPacket &packet) {
        {
            const std::lock_guard lock(mutex);
            networkPackets.push_back(packet);
        }
        changed.notify_all();
    });
    bridge.setMediaPacketHandler([&](GstBuffer *buffer) {
        if (buffer && gst_buffer_get_size(buffer) >= 12) {
            {
                const std::lock_guard lock(mutex);
                ++receivedMediaPackets;
            }
            changed.notify_all();
        }
    });
    bridge.setRtcpMinimumInterval(10 * GST_MSECOND);
    if (!bridge.start()) {
        qCritical() << "failed to start rtpsession bridge";
        return 3;
    }

    const QByteArray outgoing = makeRtp(1, 960, 0x10203040);
    GstBuffer       *out      = bufferFor(outgoing, 20 * GST_MSECOND);
    if (!out || bridge.sendRtp(out) != GST_FLOW_OK) {
        if (out)
            gst_buffer_unref(out);
        qCritical() << "failed to feed outgoing RTP";
        return 4;
    }
    gst_buffer_unref(out);

    {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, 2s, [&] {
                return std::any_of(networkPackets.cbegin(), networkPackets.cend(), [](const auto &packet) {
                    return packet.type == PsiMedia::PRtpPacket::Type::Rtp;
                });
            })) {
            qCritical() << "outgoing RTP was not emitted";
            return 5;
        }
    }

    // rtpsession applies RFC 3550 probation to a newly observed remote SSRC.
    for (quint16 sequence = 10; sequence < 13; ++sequence) {
        PsiMedia::PRtpPacket packet;
        packet.rawValue = makeRtp(sequence, quint32(sequence) * 960, 0x55667788);
        packet.type     = PsiMedia::PRtpPacket::Type::Rtp;
        if (bridge.receivePacket(packet) != GST_FLOW_OK) {
            qCritical() << "failed to feed incoming RTP";
            return 6;
        }
    }
    {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, 2s, [&] { return receivedMediaPackets > 0; })) {
            qCritical() << "incoming RTP did not reach the media side";
            return 7;
        }
    }

    PsiMedia::PRtpPacket rr;
    rr.rawValue = makeReceiverReport(0x99aabbcc);
    rr.type     = PsiMedia::PRtpPacket::Type::Rtcp;
    if (bridge.receivePacket(rr) != GST_FLOW_OK) {
        qCritical() << "failed to feed incoming RTCP";
        return 8;
    }
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (bridge.receivedRtcpPackets() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    if (bridge.receivedRtcpPackets() == 0) {
        qCritical() << "incoming RTCP was not observed by rtpsession";
        return 9;
    }

    if (!bridge.requestRtcp(0)) {
        qCritical() << "rtpsession refused to schedule RTCP";
        return 10;
    }
    {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, 2s, [&] {
                return std::any_of(networkPackets.cbegin(), networkPackets.cend(), [](const auto &packet) {
                    return packet.type == PsiMedia::PRtpPacket::Type::Rtcp && isRtcp(packet.rawValue);
                });
            })) {
            qCritical() << "outgoing RTCP was not emitted";
            return 11;
        }
    }

    bridge.stop();
    qInfo() << "RTP/RTCP session bridge regression passed";
    return 0;
}
