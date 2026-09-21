/*
 * Copyright (C) 2026 Psi IM team
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "rtpgroupbridge.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

using namespace PsiMedia;
using namespace std::chrono_literals;

namespace {

void check(bool value, const char *message)
{
    if (!value)
        qFatal("%s", message);
}

template<typename Predicate> bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (predicate())
            return true;
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

void put16(uchar *p, quint16 value)
{
    p[0] = uchar(value >> 8);
    p[1] = uchar(value);
}

void put32(uchar *p, quint32 value)
{
    p[0] = uchar(value >> 24);
    p[1] = uchar(value >> 16);
    p[2] = uchar(value >> 8);
    p[3] = uchar(value);
}

QByteArray makeRtp(quint8 pt, quint16 sequence, quint32 timestamp, quint32 ssrc)
{
    QByteArray packet(13, '\0');
    auto      *p = reinterpret_cast<uchar *>(packet.data());
    p[0]         = 0x80;
    p[1]         = pt;
    put16(p + 2, sequence);
    put32(p + 4, timestamp);
    put32(p + 8, ssrc);
    p[12] = 0x7f;
    return packet;
}

GstBuffer *bufferFor(const QByteArray &data, GstClockTime pts)
{
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, gsize(data.size()), nullptr);
    check(buffer != nullptr, "failed to allocate RTP buffer");
    check(gst_buffer_fill(buffer, 0, data.constData(), gsize(data.size())) == gsize(data.size()),
          "failed to fill RTP buffer");
    GST_BUFFER_PTS(buffer)      = pts;
    GST_BUFFER_DTS(buffer)      = pts;
    GST_BUFFER_DURATION(buffer) = 20 * GST_MSECOND;
    return buffer;
}

QByteArray senderReport(quint32 ssrc)
{
    QByteArray packet(28, '\0');
    auto      *p = reinterpret_cast<uchar *>(packet.data());
    p[0]         = 0x80;
    p[1]         = 200;
    put16(p + 2, 6);
    put32(p + 4, ssrc);
    return packet;
}

PPayloadInfo payload(int id, const char *name, int clockrate, int channels = -1)
{
    PPayloadInfo result;
    result.id        = id;
    result.name      = QString::fromLatin1(name);
    result.clockrate = clockrate;
    result.channels  = channels;
    return result;
}

RtpGroupBridge::Endpoint endpoint(const QByteArray &id, const QString &media, const PPayloadInfo &payload,
                                  quint32 incomingSsrc, quint32 localSsrc)
{
    RtpGroupBridge::Endpoint result;
    result.id             = id;
    result.media          = media;
    result.localPayloads  = { payload };
    result.remotePayloads = { payload };
    result.route.endpointId = id;
    result.route.incomingPayloadTypes.insert(quint8(payload.id));
    result.route.incomingSsrcs.insert(incomingSsrc);
    result.route.localSsrcs.insert(localSsrc);
    return result;
}

bool containsPacket(const std::vector<PRtpPacket> &packets, const QByteArray &bytes)
{
    return std::any_of(packets.cbegin(), packets.cend(), [&](const auto &packet) {
        return packet.type == PRtpPacket::Type::Rtp && packet.rawValue == bytes;
    });
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    gst_init(&argc, &argv);

    constexpr quint32 AudioLocal  = 0x10203040;
    constexpr quint32 VideoLocal  = 0x20304050;
    constexpr quint32 AudioRemote = 0x55667788;
    constexpr quint32 VideoRemote = 0x66778899;

    const auto opus = payload(111, "OPUS", 48000, 2);
    const auto vp8  = payload(96, "VP8", 90000);
    const auto audio = endpoint(QByteArrayLiteral("audio"), QStringLiteral("audio"), opus, AudioRemote, AudioLocal);
    const auto video = endpoint(QByteArrayLiteral("video"), QStringLiteral("video"), vp8, VideoRemote, VideoLocal);

    RtpGroupBridge group;
    check(group.isValid(), "failed to construct shared RTP group");
    check(group.configure({ audio, video }), "audio+video group configuration failed");
    check(group.endpointCount() == 2, "group endpoint count mismatch");

    std::vector<PRtpPacket> networkPackets;
    int                     audioDeliveries = 0;
    int                     videoDeliveries = 0;
    bool                    runtimeError    = false;
    QThread                *ownerThread     = QThread::currentThread();
    bool                    wrongThread     = false;

    group.setNetworkPacketHandler([&](const PRtpPacket &packet) {
        wrongThread |= QThread::currentThread() != ownerThread;
        networkPackets.push_back(packet);
    });
    group.setEndpointMediaPacketHandler(QByteArrayLiteral("audio"), [&](GstBuffer *) {
        wrongThread |= QThread::currentThread() != ownerThread;
        ++audioDeliveries;
    });
    group.setEndpointMediaPacketHandler(QByteArrayLiteral("video"), [&](GstBuffer *) {
        wrongThread |= QThread::currentThread() != ownerThread;
        ++videoDeliveries;
    });
    group.setRuntimeErrorHandler([&] { runtimeError = true; });
    group.setRtcpMinimumInterval(10 * GST_MSECOND);
    check(group.start(), "failed to start shared RTP group");

    const QByteArray audioOut = makeRtp(111, 1, 960, AudioLocal);
    GstBuffer *audioBuffer = bufferFor(audioOut, 20 * GST_MSECOND);
    check(group.sendRtp(QByteArrayLiteral("audio"), audioBuffer, 0) == GST_FLOW_OK,
          "failed to send audio through shared session");
    gst_buffer_unref(audioBuffer);

    const QByteArray videoOut = makeRtp(96, 1, 3000, VideoLocal);
    GstBuffer *videoBuffer = bufferFor(videoOut, 33 * GST_MSECOND);
    check(group.sendRtp(QByteArrayLiteral("video"), videoBuffer, 0) == GST_FLOW_OK,
          "failed to send video through shared session");
    gst_buffer_unref(videoBuffer);

    check(waitUntil([&] { return containsPacket(networkPackets, audioOut) && containsPacket(networkPackets, videoOut); }),
          "shared session did not emit both outgoing RTP streams");

    // Exercise different RTP clocks in the same rtpsession. Both remote sources
    // need probation packets before the shared session releases media.
    for (quint16 sequence = 10; sequence < 13; ++sequence) {
        PRtpPacket packet;
        packet.type     = PRtpPacket::Type::Rtp;
        packet.rawValue = makeRtp(111, sequence, quint32(sequence) * 960, AudioRemote);
        check(group.receivePacket(packet) == GST_FLOW_OK, "incoming audio rejected");
    }
    for (quint16 sequence = 20; sequence < 23; ++sequence) {
        PRtpPacket packet;
        packet.type     = PRtpPacket::Type::Rtp;
        packet.rawValue = makeRtp(96, sequence, quint32(sequence) * 3000, VideoRemote);
        check(group.receivePacket(packet) == GST_FLOW_OK, "incoming video rejected");
    }

    check(waitUntil([&] { return audioDeliveries > 0 && videoDeliveries > 0; }),
          "shared session did not dispatch both incoming media streams");

    // RTCP for both streams enters the shared rtpsession as exactly one compound
    // datagram. It is not split or delivered to endpoint RTP callbacks.
    const auto beforeRtcp = group.receivedRtcpPackets();
    PRtpPacket compound;
    compound.type     = PRtpPacket::Type::Rtcp;
    compound.rawValue = senderReport(AudioRemote) + senderReport(VideoRemote);
    check(group.receivePacket(compound) == GST_FLOW_OK, "compound group RTCP rejected");
    check(waitUntil([&] { return group.receivedRtcpPackets() == beforeRtcp + 1; }),
          "compound RTCP was not consumed exactly once by the group session");

    // Removing video changes routing/PT membership without stopping or replacing
    // the shared rtpsession. Audio must continue using its existing sender state.
    const auto revisionBeforeRemoval = group.routeRevision();
    check(group.removeEndpoint(QByteArrayLiteral("video")), "video endpoint removal failed");
    check(group.endpointCount() == 1 && group.routeRevision() != revisionBeforeRemoval,
          "endpoint removal did not commit a new route revision");

    const QByteArray audioAfter = makeRtp(111, 2, 1920, AudioLocal);
    PRtpPacket audioAfterPacket;
    audioAfterPacket.type     = PRtpPacket::Type::Rtp;
    audioAfterPacket.rawValue = audioAfter;
    check(group.sendRtp(QByteArrayLiteral("audio"), audioAfterPacket) == GST_FLOW_OK,
          "surviving audio sender stopped after video removal");
    check(waitUntil([&] { return containsPacket(networkPackets, audioAfter); }),
          "surviving audio RTP was not emitted after video removal");

    PRtpPacket removedVideo;
    removedVideo.type     = PRtpPacket::Type::Rtp;
    removedVideo.rawValue = makeRtp(96, 2, 6000, VideoLocal);
    check(group.sendRtp(QByteArrayLiteral("video"), removedVideo) == GST_FLOW_ERROR,
          "removed video endpoint still transmitted");

    PRtpPacket unknownIncoming;
    unknownIncoming.type     = PRtpPacket::Type::Rtp;
    unknownIncoming.rawValue = makeRtp(96, 30, 90000, VideoRemote);
    check(group.receivePacket(unknownIncoming) == GST_FLOW_ERROR,
          "removed video route still accepted incoming RTP");

    check(!runtimeError && !wrongThread, "group bridge runtime/threading regression");
    group.stop();

    qInfo("shared audio/video RTP group regression passed");
    return 0;
}
