// SPDX-License-Identifier: LGPL-2.1-or-later

#include "../gstprovider/rtpworker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <functional>

namespace {

struct Result {
    bool started = false;
    bool updated = false;
    bool stopped = false;
    bool failed  = false;
    int  audioPackets = 0;
    int  videoPackets = 0;
    quint32 firstAudioSsrc = 0;
    quint32 lastAudioSsrc  = 0;
};

bool spinUntil(GMainContext *context, const std::function<bool()> &done, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        g_main_context_iteration(context, false);
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return done();
}

quint32 rtpSsrc(GstBuffer *buffer)
{
    if (!buffer)
        return 0;
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        return 0;
    quint32 ssrc = 0;
    if (map.size >= 12) {
        ssrc = (quint32(map.data[8]) << 24) | (quint32(map.data[9]) << 16)
            | (quint32(map.data[10]) << 8) | quint32(map.data[11]);
    }
    gst_buffer_unmap(buffer, &map);
    return ssrc;
}

PsiMedia::PAudioParams opusParams()
{
    PsiMedia::PAudioParams audio;
    audio.codec      = QStringLiteral("opus");
    audio.sampleRate = 48000;
    audio.sampleSize = 16;
    audio.channels   = 1;
    return audio;
}

PsiMedia::PVideoParams vp8Params()
{
    PsiMedia::PVideoParams video;
    video.codec = QStringLiteral("vp8");
    video.size  = QSize(640, 480);
    video.fps   = 30;
    return video;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    gst_init(nullptr, nullptr);
    auto *context = g_main_context_default();

    PsiMedia::RtpWorker worker(context, nullptr);
    Result result;
    worker.app = &result;
    worker.cb_started = [](void *p) { static_cast<Result *>(p)->started = true; };
    worker.cb_updated = [](void *p) { static_cast<Result *>(p)->updated = true; };
    worker.cb_stopped = [](void *p) { static_cast<Result *>(p)->stopped = true; };
    worker.cb_error = [](void *p) { static_cast<Result *>(p)->failed = true; };
    worker.cb_rtpAudioOut = [](const PsiMedia::RtpWorker::EncodedRtpPacket &packet, void *p) {
        auto &r = *static_cast<Result *>(p);
        const quint32 ssrc = rtpSsrc(packet.buffer);
        if (!r.firstAudioSsrc)
            r.firstAudioSsrc = ssrc;
        r.lastAudioSsrc = ssrc;
        ++r.audioPackets;
    };
    worker.cb_rtpVideoOut = [](const PsiMedia::RtpWorker::EncodedRtpPacket &, void *p) {
        ++static_cast<Result *>(p)->videoPackets;
    };

    const QString audioSource = QStringLiteral("audiotestsrc is-live=true wave=sine");
    const QString videoSource = QStringLiteral("videotestsrc is-live=true pattern=ball");

    worker.localAudioParams = { opusParams() };
    worker.setInputDevices(audioSource, QString(), QString(), QByteArray(), false);
    worker.start();
    if (!spinUntil(context, [&] { return result.started || result.failed; }) || result.failed)
        qFatal("Could not establish audio-first sender");

    worker.transmitAudio();
    if (!spinUntil(context, [&] { return result.audioPackets >= 5; }))
        qFatal("Audio-first sender produced no RTP");
    const quint32 originalAudioSsrc = result.firstAudioSsrc;
    if (!originalAudioSsrc)
        qFatal("Audio-first sender exposed no RTP SSRC");

    // This used to call cleanupSend() merely because videoInput changed from
    // empty to non-empty, rebuilding audio and delaying video startup.
    worker.setInputDevices(audioSource, videoSource, QString(), QByteArray(), false);
    worker.localVideoParams = { vp8Params() };
    result.updated = false;
    worker.update();
    if (!spinUntil(context, [&] { return result.updated || result.failed; }, 15000) || result.failed)
        qFatal("Could not hot-add video to active audio sender");

    worker.transmitVideo();
    const int audioPacketsAtUpdate = result.audioPackets;
    if (!spinUntil(context, [&] {
            return result.videoPackets >= 5 && result.audioPackets >= audioPacketsAtUpdate + 5;
        }, 10000)) {
        qFatal("Hot-added video/audio sender did not continue producing RTP");
    }

    if (worker.localVideoPayloadInfo.isEmpty() || !worker.canTransmitVideo)
        qFatal("Hot-added video negotiation was not committed");
    if (result.lastAudioSsrc != originalAudioSsrc)
        qFatal("Adding video rebuilt the live audio sender");

    worker.stop();
    if (!spinUntil(context, [&] { return result.stopped; }))
        qFatal("Sender-order worker did not stop cleanly");

    qInfo("Audio-first video send transition regression passed");
    return 0;
}
