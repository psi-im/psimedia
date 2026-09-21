#include "gstrtpsessioncontext.h"

#include "gstthread.h"
#ifdef QT_GUI_LIB
#include "gstvideowidget.h"
#endif
#include "devices.h"

#include <climits>

#include <QPointer>
#include <QSet>
#include <QThread>

namespace PsiMedia {
namespace {

constexpr int OpusPayloadType = 111;
constexpr int Vp8PayloadType  = 96;

bool isSupportedRemotePayload(const PPayloadInfo &payload, const QString &media)
{
    if (payload.id < 0 || payload.id > 127)
        return false;
    if (media == QLatin1String("audio")) {
        return payload.name.compare(QLatin1String("OPUS"), Qt::CaseInsensitive) == 0 && payload.clockrate == 48000
            && (payload.channels <= 0 || payload.channels == 2);
    }
    if (media == QLatin1String("video")) {
        return payload.name.compare(QLatin1String("VP8"), Qt::CaseInsensitive) == 0 && payload.clockrate == 90000;
    }
    return false;
}

QList<PPayloadInfo> supportedRemotePayloads(const QList<PPayloadInfo> &remote, const QString &media)
{
    for (const auto &payload : remote) {
        if (isSupportedRemotePayload(payload, media))
            return { payload };
    }
    return {};
}

QList<PPayloadInfo> negotiatedLocalPayloads(bool enabled, bool remoteConfigured, const QList<PPayloadInfo> &remote,
                                            const QString &media)
{
    if (!enabled)
        return {};

    const auto supportedRemote = supportedRemotePayloads(remote, media);
    if (remoteConfigured && supportedRemote.isEmpty())
        return {};

    PPayloadInfo payload;
    if (media == QLatin1String("audio")) {
        payload.id        = supportedRemote.isEmpty() ? OpusPayloadType : supportedRemote.constFirst().id;
        payload.name      = QStringLiteral("OPUS");
        payload.clockrate = 48000;
        payload.channels  = 2;
    } else if (media == QLatin1String("video")) {
        payload.id        = supportedRemote.isEmpty() ? Vp8PayloadType : supportedRemote.constFirst().id;
        payload.name      = QStringLiteral("VP8");
        payload.clockrate = 90000;
    } else {
        return {};
    }
    return { payload };
}

bool hasPayloadType(GstBuffer *buffer, int payloadType)
{
    if (!buffer || payloadType < 0 || payloadType > 127 || gst_buffer_get_size(buffer) < 2)
        return false;

    guint8 bytes[2] = {};
    if (gst_buffer_extract(buffer, 0, bytes, sizeof(bytes)) != sizeof(bytes))
        return false;
    if ((bytes[0] >> 6) != 2)
        return false;
    return (bytes[1] & 0x7f) == payloadType;
}

PRtpPacket packetFromBuffer(GstBuffer *buffer)
{
    PRtpPacket packet;
    packet.type = PRtpPacket::Type::Rtp;
    if (!buffer)
        return packet;

    const gsize size = gst_buffer_get_size(buffer);
    if (!size || size > gsize(INT_MAX))
        return packet;
    packet.rawValue.resize(int(size));
    if (gst_buffer_extract(buffer, 0, packet.rawValue.data(), size) != size)
        packet.rawValue.clear();
    return packet;
}

} // namespace

GstRtpSessionContext::GstRtpSessionContext(GstMainLoop *_gstLoop, DeviceMonitor *deviceMonitor, QObject *parent,
                                           bool secureMode) :
    QObject(parent), gstLoop(_gstLoop), control(nullptr), hardwareDeviceMonitor(deviceMonitor), isStarted(false),
    isStopping(false), pending_status(false), recorder(this), audioBridge(QStringLiteral("audio")),
    videoBridge(QStringLiteral("video")), allow_writes(false), secureMode_(secureMode)
{
#ifdef QT_GUI_LIB
    outputWidget  = nullptr;
    previewWidget = nullptr;
#endif

    devices.audioOutVolume = 100;
    devices.audioInVolume  = 100;

    codecs.useLocalAudioParams = true;
    codecs.useLocalVideoParams = true;

    audioRtp.session = this;
    videoRtp.session = this;

    if (secureMode_) {
        secureGroup_ = std::make_unique<SecureRtpGroup>();
        secureGroup_->setRuntimeErrorHandler([this](SecureRtpSessionContext::Error error) {
            const auto handler = secureRuntimeErrorHandler_;
            QPointer<GstRtpSessionContext> guard(this);
            if (handler)
                handler(error);
            if (guard)
                guard->control_rtpBridgeError();
        });
    }

    connect(&recorder, SIGNAL(stopped()), SLOT(recorder_stopped()));
}

GstRtpSessionContext::~GstRtpSessionContext() { cleanup(); }

QObject *GstRtpSessionContext::qobject() { return this; }

void GstRtpSessionContext::stopRtpBridges()
{
    audioSendPayloadType.store(-1, std::memory_order_release);
    videoSendPayloadType.store(-1, std::memory_order_release);

    if (secureMode_) {
        if (secureGroup_ && secureGroupStarted_)
            secureGroup_->stop();
        secureGroupStarted_  = false;
        securePayloadsReady_ = false;
        return;
    }

    audioBridge.stop();
    videoBridge.stop();
    audioBridge.setNetworkPacketHandler({});
    audioBridge.setMediaPacketHandler({});
    videoBridge.setNetworkPacketHandler({});
    videoBridge.setMediaPacketHandler({});
}

void GstRtpSessionContext::cleanup()
{
    stopRtpBridges();

#ifdef QT_GUI_LIB
    if (outputWidget)
        outputWidget->show_frame(QImage());
    if (previewWidget)
        previewWidget->show_frame(QImage());
#endif

    codecs = RwControlConfigCodecs();

    isStarted      = false;
    isStopping     = false;
    pending_status = false;

    recorder.control = nullptr;

    write_mutex.lock();
    allow_writes = false;
    delete control;
    control = nullptr;
    write_mutex.unlock();
}

void GstRtpSessionContext::setAudioOutputDevice(const QString &deviceId)
{
    devices.audioOutId = deviceId;
    if (control)
        control->updateDevices(devices);
}

void GstRtpSessionContext::setAudioInputDevice(const QString &deviceId)
{
    devices.audioInId = deviceId;
    devices.fileNameIn.clear();
    devices.fileDataIn.clear();
    if (control)
        control->updateDevices(devices);
}

void GstRtpSessionContext::setVideoInputDevice(const QString &deviceId)
{
    devices.videoInId = deviceId;
    devices.fileNameIn.clear();
    devices.fileDataIn.clear();
    if (control)
        control->updateDevices(devices);
}

void GstRtpSessionContext::setFileInput(const QString &fileName)
{
    devices.fileNameIn = fileName;
    devices.audioInId.clear();
    devices.videoInId.clear();
    devices.fileDataIn.clear();
    if (control)
        control->updateDevices(devices);
}

void GstRtpSessionContext::setFileDataInput(const QByteArray &fileData)
{
    devices.fileDataIn = fileData;
    devices.audioInId.clear();
    devices.videoInId.clear();
    devices.fileNameIn.clear();
    if (control)
        control->updateDevices(devices);
}

void GstRtpSessionContext::setFileLoopEnabled(bool enabled)
{
    devices.loopFile = enabled;
    if (control)
        control->updateDevices(devices);
}

#ifdef QT_GUI_LIB
void GstRtpSessionContext::setVideoOutputWidget(VideoWidgetContext *widget)
{
    // no change?
    if (!outputWidget && !widget)
        return;
    if (outputWidget && outputWidget->context == widget)
        return;

    delete outputWidget;
    outputWidget = nullptr;

    if (widget)
        outputWidget = new GstVideoWidget(widget, this);

    devices.useVideoOut = widget != nullptr;
    if (control)
        control->updateDevices(devices);
}

void GstRtpSessionContext::setVideoPreviewWidget(VideoWidgetContext *widget)
{
    // no change?
    if (!previewWidget && !widget)
        return;
    if (previewWidget && previewWidget->context == widget)
        return;

    delete previewWidget;
    previewWidget = nullptr;

    if (widget)
        previewWidget = new GstVideoWidget(widget, this);

    devices.useVideoPreview = widget != nullptr;
    if (control)
        control->updateDevices(devices);
}
#endif

void GstRtpSessionContext::setRecorder(QIODevice *recordDevice)
{
    // can't assign a new recording device after stopping
    Q_ASSERT(!isStopping);

    recorder.setDevice(recordDevice);
}

void GstRtpSessionContext::stopRecording() { recorder.stop(); }

void GstRtpSessionContext::setLocalAudioPreferences(const QList<PAudioParams> &params)
{
    codecs.useLocalAudioParams = true;
    codecs.localAudioParams    = params;
}

void GstRtpSessionContext::setLocalVideoPreferences(const QList<PVideoParams> &params)
{
    codecs.useLocalVideoParams = true;
    codecs.localVideoParams    = params;
}

void GstRtpSessionContext::setMaximumSendingBitrate(int kbps) { codecs.maximumSendingBitrate = kbps; }

void GstRtpSessionContext::setRemoteAudioPreferences(const QList<PPayloadInfo> &info)
{
    codecs.useRemoteAudioPayloadInfo = true;
    codecs.remoteAudioPayloadInfo    = info;
}

void GstRtpSessionContext::setRemoteVideoPreferences(const QList<PPayloadInfo> &info)
{
    codecs.useRemoteVideoPayloadInfo = true;
    codecs.remoteVideoPayloadInfo    = info;
}

void GstRtpSessionContext::start()
{
    Q_ASSERT(!control && !isStarted);

    terminalError = false;
    write_mutex.lock();

    control = new RwControlLocal(gstLoop, hardwareDeviceMonitor, this);
    connect(control, SIGNAL(statusReady(const RwControlStatus &)), SLOT(control_statusReady(const RwControlStatus &)));
    connect(control, SIGNAL(previewFrame(const QImage &)), SLOT(control_previewFrame(const QImage &)));
    connect(control, SIGNAL(outputFrame(const QImage &)), SLOT(control_outputFrame(const QImage &)));
    connect(control, SIGNAL(audioOutputIntensityChanged(int)), SLOT(control_audioOutputIntensityChanged(int)));
    connect(control, SIGNAL(audioInputIntensityChanged(int)), SLOT(control_audioInputIntensityChanged(int)));

    control->app            = this;
    control->cb_rtpAudioOut = cb_control_rtpAudioOut;
    control->cb_rtpVideoOut = cb_control_rtpVideoOut;
    control->cb_recordData  = cb_control_recordData;

    allow_writes = true;
    write_mutex.unlock();

    recorder.control = control;

    lastStatus     = RwControlStatus();
    isStarted      = false;
    pending_status = true;
    control->start(devices, codecs);
}

void GstRtpSessionContext::updatePreferences()
{
    Q_ASSERT(control && !pending_status);

    pending_status = true;
    control->updateCodecs(codecs);
}

void GstRtpSessionContext::transmitAudio()
{
    transmit.useAudio = true;
    control->setTransmit(transmit);
}

void GstRtpSessionContext::transmitVideo()
{
    transmit.useVideo = true;
    control->setTransmit(transmit);
}

void GstRtpSessionContext::pauseAudio()
{
    transmit.useAudio = false;
    control->setTransmit(transmit);
}

void GstRtpSessionContext::pauseVideo()
{
    transmit.useVideo = false;
    control->setTransmit(transmit);
}

void GstRtpSessionContext::stop()
{
    Q_ASSERT(control && !isStopping);

    // note: it's possible to stop even if pending_status is
    //   already true.  this is so we can stop a session that
    //   is in the middle of starting.

    isStopping     = true;
    pending_status = true;
    control->stop();
}

QList<PPayloadInfo> GstRtpSessionContext::localAudioPayloadInfo() const { return lastStatus.localAudioPayloadInfo; }

QList<PPayloadInfo> GstRtpSessionContext::localVideoPayloadInfo() const { return lastStatus.localVideoPayloadInfo; }

QList<PPayloadInfo> GstRtpSessionContext::remoteAudioPayloadInfo() const { return lastStatus.remoteAudioPayloadInfo; }

QList<PPayloadInfo> GstRtpSessionContext::remoteVideoPayloadInfo() const { return lastStatus.remoteVideoPayloadInfo; }

QList<PAudioParams> GstRtpSessionContext::audioParams() const { return lastStatus.localAudioParams; }

QList<PVideoParams> GstRtpSessionContext::videoParams() const { return lastStatus.localVideoParams; }

bool GstRtpSessionContext::canTransmitAudio() const { return lastStatus.canTransmitAudio; }

bool GstRtpSessionContext::canTransmitVideo() const { return lastStatus.canTransmitVideo; }

int GstRtpSessionContext::outputVolume() const { return devices.audioOutVolume; }

void GstRtpSessionContext::setOutputVolume(int level)
{
    devices.audioOutVolume = level;
    if (control)
        control->updateDevices(devices);
}

int GstRtpSessionContext::inputVolume() const { return devices.audioInVolume; }

void GstRtpSessionContext::setInputVolume(int level)
{
    devices.audioInVolume = level;
    if (control)
        control->updateDevices(devices);
}

RtpSessionContext::Error GstRtpSessionContext::errorCode() const { return static_cast<Error>(lastStatus.errorCode); }

RtpChannelContext *GstRtpSessionContext::audioRtpChannel() { return &audioRtp; }

RtpChannelContext *GstRtpSessionContext::videoRtpChannel() { return &videoRtp; }

void GstRtpSessionContext::dumpPipeline(std::function<void(const QStringList &)> callback)
{
    if (control)
        control->dumpPipeline(callback);
    else
        callback(QStringList());
}

void GstRtpSessionContext::push_packet_for_write(GstRtpChannel *from, const PRtpPacket &rtp)
{
    {
        QMutexLocker locker(&write_mutex);
        if (!allow_writes || !control)
            return;
    }

    // Secure sessions have a protected-only network boundary. The legacy RTP
    // channels remain present for Provider/1.6 ABI compatibility but cannot be
    // used to inject plaintext network packets.
    if (secureMode_)
        return;

    if (from == &audioRtp)
        audioBridge.receivePacket(rtp);
    else if (from == &videoRtp)
        videoBridge.receivePacket(rtp);
}

bool GstRtpSessionContext::configureRtpBridges()
{
    const bool audioEnabled = codecs.useLocalAudioParams && !codecs.localAudioParams.isEmpty();
    const bool videoEnabled = codecs.useLocalVideoParams && !codecs.localVideoParams.isEmpty();

    const auto localAudio = negotiatedLocalPayloads(audioEnabled, codecs.useRemoteAudioPayloadInfo,
                                                     codecs.remoteAudioPayloadInfo, QStringLiteral("audio"));
    const auto localVideo = negotiatedLocalPayloads(videoEnabled, codecs.useRemoteVideoPayloadInfo,
                                                     codecs.remoteVideoPayloadInfo, QStringLiteral("video"));
    const auto remoteAudio = supportedRemotePayloads(codecs.remoteAudioPayloadInfo, QStringLiteral("audio"));
    const auto remoteVideo = supportedRemotePayloads(codecs.remoteVideoPayloadInfo, QStringLiteral("video"));

    lastStatus.localAudioPayloadInfo  = localAudio;
    lastStatus.localVideoPayloadInfo  = localVideo;
    lastStatus.remoteAudioPayloadInfo = remoteAudio;
    lastStatus.remoteVideoPayloadInfo = remoteVideo;

    if (secureMode_) {
        audioSendPayloadType.store(localAudio.isEmpty() ? -1 : localAudio.constFirst().id, std::memory_order_release);
        videoSendPayloadType.store(localVideo.isEmpty() ? -1 : localVideo.constFirst().id, std::memory_order_release);
        securePayloadsReady_ = true;
        return configureSecureGroup();
    }

    const auto configure = [this](RtpSessionBridge &bridge, std::atomic<int> &sendPayloadType, GstRtpChannel &channel,
                                  const QList<PPayloadInfo> &local, const QList<PPayloadInfo> &remote, bool audio) {
        if (local.isEmpty()) {
            sendPayloadType.store(-1, std::memory_order_release);
            bridge.stop();
            bridge.setNetworkPacketHandler({});
            bridge.setMediaPacketHandler({});
            bridge.setRuntimeErrorHandler({});
            return true;
        }
        if (!bridge.isValid() || !bridge.setPayloads(local, remote))
            return false;

        bridge.setRuntimeErrorHandler([this]() { control_rtpBridgeError(); });
        auto *channelPtr = &channel;
        bridge.setNetworkPacketHandler(
            [channelPtr](const PRtpPacket &packet) { channelPtr->push_packet_for_read(packet); });
        bridge.setMediaPacketHandler([this, audio](GstBuffer *buffer) {
            const auto packet = packetFromBuffer(buffer);
            if (packet.rawValue.isEmpty())
                return;

            QMutexLocker locker(&write_mutex);
            if (!allow_writes || !control)
                return;
            if (audio)
                control->rtpAudioIn(packet);
            else
                control->rtpVideoIn(packet);
        });
        sendPayloadType.store(local.constFirst().id, std::memory_order_release);
        return bridge.start();
    };

    return configure(audioBridge, audioSendPayloadType, audioRtp, localAudio, remoteAudio, true)
        && configure(videoBridge, videoSendPayloadType, videoRtp, localVideo, remoteVideo, false);
}

bool GstRtpSessionContext::configureSecureGroup()
{
    if (!secureMode_ || !secureGroup_)
        return false;
    if (!securePayloadsReady_ || secureEndpoints_.isEmpty())
        return true;

    QList<RtpGroupBridge::Endpoint> groupEndpoints;
    QByteArray nextAudioEndpointId;
    QByteArray nextVideoEndpointId;
    QSet<QString> mediaSeen;

    for (const auto &endpoint : secureEndpoints_) {
        if (endpoint.endpointId.isEmpty() || (endpoint.media != QLatin1String("audio")
                                               && endpoint.media != QLatin1String("video"))
            || mediaSeen.contains(endpoint.media))
            return false;
        mediaSeen.insert(endpoint.media);

        const bool audio = endpoint.media == QLatin1String("audio");
        const auto &local  = audio ? lastStatus.localAudioPayloadInfo : lastStatus.localVideoPayloadInfo;
        const auto &remote = audio ? lastStatus.remoteAudioPayloadInfo : lastStatus.remoteVideoPayloadInfo;
        if (local.isEmpty() || remote.isEmpty())
            return false;

        RtpGroupBridge::Endpoint groupEndpoint;
        groupEndpoint.id             = endpoint.endpointId;
        groupEndpoint.media          = endpoint.media;
        groupEndpoint.localPayloads  = local;
        groupEndpoint.remotePayloads = remote;
        groupEndpoint.route.endpointId     = endpoint.endpointId;
        groupEndpoint.route.mid            = endpoint.mid;
        groupEndpoint.route.midExtensionId = endpoint.midExtensionId;

        for (int payloadType : endpoint.incomingPayloadTypes) {
            if (payloadType < 0 || payloadType > 127)
                return false;
            groupEndpoint.route.incomingPayloadTypes.insert(quint8(payloadType));
        }
        if (groupEndpoint.route.incomingPayloadTypes.isEmpty())
            return false;

        for (quint32 ssrc : endpoint.incomingSsrcs) {
            if (ssrc)
                groupEndpoint.route.incomingSsrcs.insert(ssrc);
        }
        for (quint32 ssrc : endpoint.localSsrcs) {
            if (ssrc)
                groupEndpoint.route.localSsrcs.insert(ssrc);
        }

        if (audio)
            nextAudioEndpointId = endpoint.endpointId;
        else
            nextVideoEndpointId = endpoint.endpointId;

        groupEndpoints.append(std::move(groupEndpoint));
    }

    if (!secureGroup_->configureEndpoints(groupEndpoints))
        return false;

    audioSecureEndpointId_ = nextAudioEndpointId;
    videoSecureEndpointId_ = nextVideoEndpointId;

    for (const auto &endpoint : groupEndpoints) {
        const bool audio = endpoint.media == QLatin1String("audio");
        secureGroup_->setEndpointMediaPacketHandler(endpoint.id, [this, audio](GstBuffer *buffer) {
            const auto packet = packetFromBuffer(buffer);
            if (packet.rawValue.isEmpty())
                return;

            QMutexLocker locker(&write_mutex);
            if (!allow_writes || !control)
                return;
            if (audio)
                control->rtpAudioIn(packet);
            else
                control->rtpVideoIn(packet);
        });
    }

    return maybeStartSecureGroup();
}

bool GstRtpSessionContext::maybeStartSecureGroup()
{
    if (!secureMode_ || !secureGroup_)
        return false;
    if (secureGroupStarted_)
        return true;
    if (!securePayloadsReady_ || secureEndpoints_.isEmpty() || !secureGroup_->isReady())
        return true;

    if (!secureGroup_->start())
        return false;
    secureGroupStarted_ = true;
    return true;
}

bool GstRtpSessionContext::secureConfigureEndpoints(const QList<PSecureRtpEndpoint> &endpoints)
{
    if (!secureMode_ || !secureGroup_ || QThread::currentThread() != thread() || endpoints.isEmpty())
        return false;

    QSet<QByteArray> ids;
    QSet<QString>    media;
    for (const auto &endpoint : endpoints) {
        if (endpoint.endpointId.isEmpty() || ids.contains(endpoint.endpointId)
            || (endpoint.media != QLatin1String("audio") && endpoint.media != QLatin1String("video"))
            || media.contains(endpoint.media))
            return false;
        ids.insert(endpoint.endpointId);
        media.insert(endpoint.media);
        for (int payloadType : endpoint.incomingPayloadTypes) {
            if (payloadType < 0 || payloadType > 127)
                return false;
        }
    }

    const auto previous = secureEndpoints_;
    secureEndpoints_ = endpoints;
    if (securePayloadsReady_ && !configureSecureGroup()) {
        secureEndpoints_ = previous;
        return false;
    }
    return true;
}

bool GstRtpSessionContext::secureConfigure(const QByteArray &associationId, quint64 epoch, const QString &profile,
                                     const QByteArray &localMasterKey, const QByteArray &localMasterSalt,
                                     const QByteArray &remoteMasterKey, const QByteArray &remoteMasterSalt)
{
    if (!secureMode_ || !secureGroup_ || QThread::currentThread() != thread())
        return false;
    if (!secureGroup_->activate(associationId, epoch, profile, localMasterKey, localMasterSalt, remoteMasterKey,
                                remoteMasterSalt))
        return false;
    return maybeStartSecureGroup();
}

void GstRtpSessionContext::secureInvalidate(const QByteArray &associationId, quint64 epoch)
{
    if (!secureMode_ || !secureGroup_ || QThread::currentThread() != thread())
        return;
    secureGroup_->invalidate(associationId, epoch);
}

bool GstRtpSessionContext::secureIsReady() const
{
    return secureMode_ && secureGroup_ && secureGroup_->isReady();
}

QByteArray GstRtpSessionContext::secureAssociationId() const
{
    return secureMode_ && secureGroup_ ? secureGroup_->associationId() : QByteArray();
}

quint64 GstRtpSessionContext::secureEpoch() const
{
    return secureMode_ && secureGroup_ ? secureGroup_->epoch() : 0;
}

SecureRtpSessionContext::Error GstRtpSessionContext::secureLastError() const
{
    return secureMode_ && secureGroup_ ? secureGroup_->lastError() : SecureRtpSessionContext::Error::NotReady;
}

void GstRtpSessionContext::secureSetProtectedPacketHandler(SecureRtpSessionContext::ProtectedPacketHandler handler)
{
    if (!secureMode_ || !secureGroup_ || QThread::currentThread() != thread())
        return;
    secureGroup_->setProtectedPacketHandler(std::move(handler));
}

void GstRtpSessionContext::secureSetRuntimeErrorHandler(SecureRtpSessionContext::RuntimeErrorHandler handler)
{
    if (!secureMode_ || QThread::currentThread() != thread())
        return;
    secureRuntimeErrorHandler_ = std::move(handler);
}

bool GstRtpSessionContext::secureReceiveProtectedPacket(const PSecureRtpPacket &packet)
{
    return secureMode_ && secureGroup_ && QThread::currentThread() == thread()
        && secureGroup_->receiveProtectedPacket(packet);
}

void GstRtpSessionContext::control_statusReady(const RwControlStatus &status)
{
    if (terminalError)
        return;

    lastStatus = status;

    if (!status.finished && !status.error && pending_status && !status.stopped && !isStopping) {
        if (!configureRtpBridges()) {
            terminalError        = true;
            lastStatus.error     = true;
            lastStatus.errorCode = int(ErrorGeneric);
            cleanup();
            emit error();
            return;
        }
    }

    if (status.finished) {
        // finished status just means the file is done
        //   sending.  the session still remains active.
        emit finished();
    } else if (status.error) {
        terminalError = true;
        cleanup();
        emit error();
        return;
    } else if (pending_status) {
        if (status.stopped) {
            pending_status = false;

            cleanup();
            emit stopped();
            return;
        }

        // if we're currently stopping, ignore all other
        //   pending status events except for stopped
        //   (handled above)
        if (isStopping)
            return;

        pending_status = false;

        if (!isStarted) {
            isStarted = true;

            // if there was a pending record, start it
            recorder.startNext();

            emit started();
        } else
            emit preferencesUpdated();
    }
}

void GstRtpSessionContext::control_previewFrame(const QImage &img)
{
#ifdef QT_GUI_LIB
    if (previewWidget)
        previewWidget->show_frame(img);
#else
    Q_UNUSED(img)
#endif
}

void GstRtpSessionContext::control_outputFrame(const QImage &img)
{
#ifdef QT_GUI_LIB
    if (outputWidget)
        outputWidget->show_frame(img);
#else
    Q_UNUSED(img)
#endif
}

void GstRtpSessionContext::control_audioOutputIntensityChanged(int intensity)
{
    emit audioOutputIntensityChanged(intensity);
}

void GstRtpSessionContext::control_audioInputIntensityChanged(int intensity)
{
    emit audioInputIntensityChanged(intensity);
}

void GstRtpSessionContext::control_rtpBridgeError()
{
    if (terminalError || isStopping || !control)
        return;

    terminalError        = true;
    lastStatus.error     = true;
    lastStatus.errorCode = int(ErrorGeneric);
    cleanup();
    emit error();
}

void GstRtpSessionContext::recorder_stopped() { emit stoppedRecording(); }

void GstRtpSessionContext::cb_control_rtpAudioOut(const RtpWorker::EncodedRtpPacket &packet, void *app)
{
    static_cast<GstRtpSessionContext *>(app)->control_rtpAudioOut(packet);
}

void GstRtpSessionContext::cb_control_rtpVideoOut(const RtpWorker::EncodedRtpPacket &packet, void *app)
{
    static_cast<GstRtpSessionContext *>(app)->control_rtpVideoOut(packet);
}

void GstRtpSessionContext::cb_control_recordData(const QByteArray &packet, void *app)
{
    static_cast<GstRtpSessionContext *>(app)->control_recordData(packet);
}

void GstRtpSessionContext::control_rtpAudioOut(const RtpWorker::EncodedRtpPacket &packet)
{
    if (!hasPayloadType(packet.buffer, audioSendPayloadType.load(std::memory_order_acquire)))
        return;
    if (!secureMode_) {
        audioBridge.sendRtp(packet.buffer, packet.presentationAge);
        return;
    }

    GstBuffer *buffer = gst_buffer_ref(packet.buffer);
    const auto age = packet.presentationAge;
    QMetaObject::invokeMethod(this, [this, buffer, age]() {
        if (secureGroup_ && secureGroupStarted_ && !audioSecureEndpointId_.isEmpty())
            secureGroup_->sendRtp(audioSecureEndpointId_, buffer, age);
        gst_buffer_unref(buffer);
    }, Qt::QueuedConnection);
}

void GstRtpSessionContext::control_rtpVideoOut(const RtpWorker::EncodedRtpPacket &packet)
{
    if (!hasPayloadType(packet.buffer, videoSendPayloadType.load(std::memory_order_acquire)))
        return;
    if (!secureMode_) {
        videoBridge.sendRtp(packet.buffer, packet.presentationAge);
        return;
    }

    GstBuffer *buffer = gst_buffer_ref(packet.buffer);
    const auto age = packet.presentationAge;
    QMetaObject::invokeMethod(this, [this, buffer, age]() {
        if (secureGroup_ && secureGroupStarted_ && !videoSecureEndpointId_.isEmpty())
            secureGroup_->sendRtp(videoSecureEndpointId_, buffer, age);
        gst_buffer_unref(buffer);
    }, Qt::QueuedConnection);
}

void GstRtpSessionContext::control_recordData(const QByteArray &packet) { recorder.push_data_for_read(packet); }

} // namespace PsiMedia
