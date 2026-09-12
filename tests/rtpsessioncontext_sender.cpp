/*
 * Copyright (C) 2026  Psi IM team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "gstprovider.h"
#include "gstrtpchannel.h"
#include "gstrtpsessioncontext.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <memory>

namespace {

constexpr int NegotiatedPayloadType = 109;
constexpr int RawSampleRate         = 44100;
constexpr int RawChannels           = 1;
constexpr int OpusRtpClockRate      = 48000;
constexpr int OpusRtpChannels       = 2;

bool isExpectedRtpPacket(const PsiMedia::PRtpPacket &packet)
{
    if (packet.type != PsiMedia::PRtpPacket::Type::Rtp || packet.rawValue.size() < 2)
        return false;

    const auto *bytes = reinterpret_cast<const uchar *>(packet.rawValue.constData());
    return (bytes[0] >> 6) == 2 && (bytes[1] & 0x7f) == NegotiatedPayloadType;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    PsiMedia::GstProvider provider;
    if (!provider.isInitialized()) {
        qCritical() << "GStreamer provider failed to initialize";
        return 1;
    }

    std::unique_ptr<PsiMedia::RtpSessionContext> session(provider.createRtpSession());
    auto *gstSession = qobject_cast<PsiMedia::GstRtpSessionContext *>(session->qobject());
    auto *audioChannel = qobject_cast<PsiMedia::GstRtpChannel *>(session->audioRtpChannel()->qobject());
    if (!gstSession || !audioChannel) {
        qCritical() << "Provider did not create the production GStreamer RTP session";
        return 2;
    }

    PsiMedia::PAudioParams rawAudio;
    rawAudio.codec      = QStringLiteral("opus");
    rawAudio.sampleRate = RawSampleRate;
    rawAudio.sampleSize = 16;
    rawAudio.channels   = RawChannels;
    session->setLocalAudioPreferences({ rawAudio });

    PsiMedia::PPayloadInfo remoteOpus;
    remoteOpus.id        = NegotiatedPayloadType;
    remoteOpus.name      = QStringLiteral("OPUS");
    remoteOpus.clockrate = OpusRtpClockRate;
    remoteOpus.channels  = OpusRtpChannels;
    session->setRemoteAudioPreferences({ remoteOpus });

    // Device IDs are GStreamer launch descriptions in the production provider.
    // This exercises the real AudioIn pipeline without requiring physical hardware.
    session->setAudioInputDevice(QStringLiteral("audiotestsrc is-live=true wave=sine"));
    audioChannel->setEnabled(true);

    bool       startFailed = false;
    bool       startTimedOut = false;
    QEventLoop startLoop;
    QTimer     startTimer;
    startTimer.setSingleShot(true);
    QObject::connect(&startTimer, &QTimer::timeout, &startLoop, [&]() {
        startTimedOut = true;
        startLoop.quit();
    });
    QObject::connect(gstSession, &PsiMedia::GstRtpSessionContext::started, &startLoop, &QEventLoop::quit);
    QObject::connect(gstSession, &PsiMedia::GstRtpSessionContext::error, &startLoop, [&]() {
        startFailed = true;
        startLoop.quit();
    });

    startTimer.start(10000);
    session->start();
    startLoop.exec();
    startTimer.stop();

    if (startFailed || startTimedOut) {
        qCritical() << (startFailed ? "Production RTP session failed to start" : "Timed out starting production RTP session");
        return 3;
    }

    const auto localPayloads = session->localAudioPayloadInfo();
    if (!session->canTransmitAudio() || localPayloads.size() != 1) {
        qCritical() << "Production RTP session did not expose one transmittable Opus payload";
        return 4;
    }

    const auto &localOpus = localPayloads.constFirst();
    if (localOpus.id != NegotiatedPayloadType
        || localOpus.name.compare(QStringLiteral("OPUS"), Qt::CaseInsensitive) != 0
        || localOpus.clockrate != OpusRtpClockRate || localOpus.channels != OpusRtpChannels) {
        qCritical() << "Unexpected negotiated Opus payload" << localOpus.id << localOpus.name << localOpus.clockrate
                    << localOpus.channels;
        return 5;
    }

    bool                  packetTimedOut = false;
    bool                  sessionFailed  = false;
    PsiMedia::PRtpPacket  packet;
    QEventLoop            packetLoop;
    QTimer                packetTimer;
    packetTimer.setSingleShot(true);
    QObject::connect(&packetTimer, &QTimer::timeout, &packetLoop, [&]() {
        packetTimedOut = true;
        packetLoop.quit();
    });
    QObject::connect(gstSession, &PsiMedia::GstRtpSessionContext::error, &packetLoop, [&]() {
        sessionFailed = true;
        packetLoop.quit();
    });
    QObject::connect(audioChannel, &PsiMedia::GstRtpChannel::readyRead, &packetLoop, [&]() {
        while (audioChannel->packetsAvailable() > 0) {
            const auto candidate = audioChannel->read();
            if (isExpectedRtpPacket(candidate)) {
                packet = candidate;
                packetLoop.quit();
                return;
            }
        }
    });

    packetTimer.start(10000);
    session->transmitAudio();
    packetLoop.exec();
    packetTimer.stop();

    if (sessionFailed || packetTimedOut || packet.rawValue.isEmpty()) {
        qCritical() << (sessionFailed ? "Production RTP sender failed"
                                     : "Timed out waiting for negotiated RTP from production sender");
        return 6;
    }

    session->pauseAudio();

    bool       stopTimedOut = false;
    QEventLoop stopLoop;
    QTimer     stopTimer;
    stopTimer.setSingleShot(true);
    QObject::connect(&stopTimer, &QTimer::timeout, &stopLoop, [&]() {
        stopTimedOut = true;
        stopLoop.quit();
    });
    QObject::connect(gstSession, &PsiMedia::GstRtpSessionContext::stopped, &stopLoop, &QEventLoop::quit);

    stopTimer.start(10000);
    session->stop();
    stopLoop.exec();
    stopTimer.stop();

    if (stopTimedOut) {
        qCritical() << "Timed out stopping production RTP session";
        return 7;
    }

    qInfo() << "Production sender emitted Opus RTP PT" << NegotiatedPayloadType << "from synthetic raw audio"
            << RawSampleRate << "Hz /" << RawChannels << "channel with RTP description" << OpusRtpClockRate << "Hz /"
            << OpusRtpChannels << "channels";
    return 0;
}