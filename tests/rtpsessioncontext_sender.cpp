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
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

#include <gst/gst.h>

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


QString receiveAppSrcName(PsiMedia::RtpSessionContext *session, int timeoutMs = 5000)
{
    QString    dotPath;
    QEventLoop loop;
    QTimer     timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    session->dumpPipeline([&](const QStringList &paths) {
        QString recvPath;
        for (const auto &path : paths) {
            if (path.endsWith(QStringLiteral("psimedia_recv.dot"))) {
                recvPath = path;
                break;
            }
        }
        QMetaObject::invokeMethod(
            &loop,
            [&, recvPath]() {
                dotPath = recvPath;
                loop.quit();
            },
            Qt::QueuedConnection);
    });

    timer.start(timeoutMs);
    loop.exec();
    if (dotPath.isEmpty())
        return {};

    QFile file(dotPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString dot = QString::fromUtf8(file.readAll());
    const auto match = QRegularExpression(QStringLiteral("psimedia_audio_rtp_recv_\\d+")).match(dot);
    return match.hasMatch() ? match.captured(0) : QString();
}

void drainPackets(PsiMedia::GstRtpChannel *channel)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    while (channel->packetsAvailable() > 0)
        channel->read();
}


bool createFiniteOpusFile(const QString &path)
{
    GstElement *pipeline = gst_pipeline_new("psimedia-test-file");
    GstElement *source   = gst_element_factory_make("audiotestsrc", nullptr);
    GstElement *convert  = gst_element_factory_make("audioconvert", nullptr);
    GstElement *resample = gst_element_factory_make("audioresample", nullptr);
    GstElement *caps     = gst_element_factory_make("capsfilter", nullptr);
    GstElement *encoder  = gst_element_factory_make("opusenc", nullptr);
    GstElement *mux      = gst_element_factory_make("oggmux", nullptr);
    GstElement *sink     = gst_element_factory_make("filesink", nullptr);
    if (!pipeline || !source || !convert || !resample || !caps || !encoder || !mux || !sink) {
        if (pipeline)
            gst_object_unref(pipeline);
        return false;
    }

    g_object_set(source, "is-live", FALSE, "num-buffers", 50, nullptr);
    g_object_set(sink, "location", QFile::encodeName(path).constData(), nullptr);
    GstCaps *rawCaps = gst_caps_new_simple("audio/x-raw", "rate", G_TYPE_INT, OpusRtpClockRate, "channels",
                                           G_TYPE_INT, OpusRtpChannels, nullptr);
    g_object_set(caps, "caps", rawCaps, nullptr);
    gst_caps_unref(rawCaps);

    gst_bin_add_many(GST_BIN(pipeline), source, convert, resample, caps, encoder, mux, sink, nullptr);
    if (!gst_element_link_many(source, convert, resample, caps, encoder, mux, sink, nullptr)) {
        gst_object_unref(pipeline);
        return false;
    }

    const auto stateResult = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (stateResult == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return false;
    }

    GstBus     *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(
        bus, 10 * GST_SECOND, GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS;
    if (msg)
        gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return ok && QFileInfo(path).size() > 0;
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

    QTemporaryDir tempDir;
    const QString filePath = tempDir.filePath(QStringLiteral("switch.ogg"));
    if (!tempDir.isValid()) {
        qCritical() << "Could not create temporary test directory";
        return 6;
    }
    qputenv("GST_DEBUG_DUMP_DOT_DIR", QFile::encodeName(tempDir.path()));
    if (!createFiniteOpusFile(filePath)) {
        qCritical() << "Could not create finite Ogg/Opus test input";
        return 6;
    }

    const QString receiveBeforeSwitch = receiveAppSrcName(session.get());
    if (receiveBeforeSwitch.isEmpty()) {
        qCritical() << "Could not identify the production receive appsrc";
        return 7;
    }

    // Start with an unbounded live source. A live->file switch must tear this
    // capture source down before the finite file source is committed.
    session->setAudioInputDevice(QStringLiteral("audiotestsrc is-live=true wave=sine"));
    if (!waitForControlBarrier(session.get())) {
        qCritical() << "Timed out attaching the live synthetic audio input";
        return 7;
    }
    session->transmitAudio();
    if (!waitForRtp(audioChannel, gstSession)) {
        qCritical() << "Timed out waiting for RTP from the live input";
        return 8;
    }

    drainPackets(audioChannel);
    bool       fileFinished = false;
    bool       fileFailed   = false;
    QEventLoop fileLoop;
    QTimer     fileTimer;
    fileTimer.setSingleShot(true);
    QObject::connect(&fileTimer, &QTimer::timeout, &fileLoop, &QEventLoop::quit);
    QObject::connect(gstSession, &PsiMedia::GstRtpSessionContext::finished, &fileLoop, [&]() {
        fileFinished = true;
        fileLoop.quit();
    });
    QObject::connect(gstSession, &PsiMedia::GstRtpSessionContext::error, &fileLoop, [&]() {
        fileFailed = true;
        fileLoop.quit();
    });

    // Install the completion latch before replacing the live source: a finite
    // non-live file may reach EOS faster than the control-barrier round trip.
    session->setFileInput(filePath);
    if (!waitForControlBarrier(session.get())) {
        qCritical() << "Timed out switching from live input to file input";
        return 9;
    }
    const QString receiveAfterFileSwitch = receiveAppSrcName(session.get());
    if (receiveAfterFileSwitch != receiveBeforeSwitch) {
        qCritical() << "Live-to-file capture switch recreated the receive pipeline"
                    << receiveBeforeSwitch << receiveAfterFileSwitch;
        return 10;
    }
    // Preserve the previous transmit intent across the source replacement.
    session->transmitAudio();
    if (!waitForRtp(audioChannel, gstSession)) {
        qCritical() << "File input did not produce RTP after replacing live capture";
        return 10;
    }
    if (!fileFinished && !fileFailed) {
        fileTimer.start(10000);
        fileLoop.exec();
    }
    if (!fileFinished || fileFailed) {
        qCritical() << "Finite file input did not replace the unbounded live source";
        return 11;
    }

    // The session/bridge survives source replacement. Switching back to a
    // different live source must rebuild capture and resume RTP without
    // recreating GstRtpSessionContext.
    session->setAudioInputDevice(QStringLiteral("audiotestsrc is-live=true wave=white-noise"));
    if (!waitForControlBarrier(session.get())) {
        qCritical() << "Timed out switching from file input back to live input";
        return 12;
    }
    const QString receiveAfterLiveSwitch = receiveAppSrcName(session.get());
    if (receiveAfterLiveSwitch != receiveBeforeSwitch) {
        qCritical() << "File-to-live capture switch recreated the receive pipeline"
                    << receiveBeforeSwitch << receiveAfterLiveSwitch;
        return 13;
    }
    session->transmitAudio();
    if (!waitForRtp(audioChannel, gstSession)) {
        qCritical() << "RTP did not resume after file-to-live switch";
        return 13;
    }

    session->pauseAudio();
    session->setAudioInputDevice(QString());
    if (!waitForControlBarrier(session.get())) {
        qCritical() << "Timed out detaching the final synthetic audio input";
        return 14;
    }
    drainPackets(audioChannel);

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
        return 15;
    }

    qInfo() << "Production RTP sender replaced live/file capture sources and resumed Opus RTP PT"
            << NegotiatedPayloadType << "without recreating the RTP session";
    return 0;
}
