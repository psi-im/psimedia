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
#include <QMetaObject>
#include <QTimer>

#include <memory>
#include <optional>

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

std::optional<PsiMedia::PRtpPacket> waitForRtp(PsiMedia::GstRtpChannel *audioChannel,
                                               PsiMedia::GstRtpSessionContext *session, int timeoutMs = 10000)
{
    std::optional<PsiMedia::PRtpPacket> result;
    QEventLoop                          loop;
    QTimer                              timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(session, &PsiMedia::GstRtpSessionContext::error, &loop, &QEventLoop::quit);
    QObject::connect(audioChannel, &PsiMedia::GstRtpChannel::readyRead, &loop, [&]() {
        while (audioChannel->packetsAvailable() > 0) {
            const auto packet = audioChannel->read();
            if (isExpectedRtpPacket(packet)) {
                result = packet;
                loop.quit();
                return;
            }
        }
    });

    timer.start(timeoutMs);
    loop.exec();
    return result;
}

bool waitForControlBarrier(PsiMedia::RtpSessionContext *session, int timeoutMs = 5000)
{
    bool       done = false;
    QEventLoop loop;
    QTimer     timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    session->dumpPipeline([&](const QStringList &) {
        QMetaObject::invokeMethod(
            &loop,
            [&]() {
                done = true;
                loop.quit();
            },
            Qt::QueuedConnection);
    });

    timer.start(timeoutMs);
    loop.exec();
    return done;
}

void drainPackets(PsiMedia::GstRtpChannel *channel)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    while (channel->packetsAvailable() > 0)
        channel->read();
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

    // Negotiation must not require capture. The live source is attached only
    // after the session has started, matching Psi's consent/no-microphone path.
    audioChannel->setEnabled(true);

    bool       startFailed   = false;
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
        qCritical() << (startFailed ? "Production RTP session failed to start"
                                   : "Timed out starting production RTP session");
        return 3;
    }

    const auto localPayloads = session->localAudioPayloadInfo();
    if (localPayloads.size() != 1) {
        qCritical() << "Device-independent negotiation did not expose one Opus payload";
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

    // A finite first source makes a stale send pipeline observable: once it has
    // reached EOS it cannot produce packets after the second source is selected.
    session->setAudioInputDevice(QStringLiteral("audiotestsrc is-live=true wave=sine num-buffers=32"));
    if (!waitForControlBarrier(session.get())) {
        qCritical() << "Timed out attaching the first synthetic audio input";
        return 6;
    }
    session->transmitAudio();
    if (!waitForRtp(audioChannel, gstSession)) {
        qCritical() << "Timed out waiting for RTP after late audio-input attach";
        return 7;
    }

    // Let the finite source reach EOS, then detach it exactly as the native
    // call path does when capture consent/device availability disappears.
    QEventLoop drainLoop;
    QTimer::singleShot(1000, &drainLoop, &QEventLoop::quit);
    drainLoop.exec();
    session->pauseAudio();
    session->setAudioInputDevice(QString());
    if (!waitForControlBarrier(session.get())) {
        qCritical() << "Timed out detaching the synthetic audio input";
        return 8;
    }
    drainPackets(audioChannel);

    // Reattach a different live source without recreating RtpSessionContext or
    // RtpSessionBridge. Before the fix RtpWorker kept the exhausted old sendbin
    // and this phase timed out.
    session->setAudioInputDevice(QStringLiteral("audiotestsrc is-live=true wave=white-noise"));
    if (!waitForControlBarrier(session.get())) {
        qCritical() << "Timed out reattaching the synthetic audio input";
        return 9;
    }
    session->transmitAudio();
    if (!waitForRtp(audioChannel, gstSession)) {
        qCritical() << "RTP did not resume after audio-input hotplug";
        return 10;
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
        return 11;
    }

    qInfo() << "Production RTP sender negotiated without capture and resumed Opus RTP PT" << NegotiatedPayloadType
            << "after audio-input hotplug";
    return 0;
}
