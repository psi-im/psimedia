#include "gstaudiorecordercontext.h"

namespace PsiMedia {

GstAudioRecorderContext::GstAudioRecorderContext(GstMainLoop *_gstLoop, QObject *parent) :
    QObject(parent), gstLoop(_gstLoop), recorder(this)
{
    connect(&recorder, &GstRecorder::stopped, this, &GstAudioRecorderContext::stopped);
}

GstAudioRecorderContext::~GstAudioRecorderContext() { cleanup(); }

QObject *GstAudioRecorderContext::qobject() { return this; }

void GstAudioRecorderContext::cleanup()
{

    isStarted     = false;
    isStopping    = false;
    pendingStatus = false;

    recorder.control = nullptr;

    writeMutex.lock();
    allowWrites = false;
    delete control;
    control = nullptr;
    writeMutex.unlock();
}

void GstAudioRecorderContext::setInputDevice(const QString &deviceId)
{
    devices.audioInId = deviceId;
    devices.fileNameIn.clear();
    devices.fileDataIn.clear();
    if (control)
        control->updateDevices(devices);
}

void GstAudioRecorderContext::setOutputDevice(QIODevice *recordDevice)
{
    // can't assign a new recording device after stopping
    Q_ASSERT(!isStopping);

    recorder.setDevice(recordDevice);
}

void GstAudioRecorderContext::setPreferences(const QList<PAudioParams> &params) { Q_UNUSED(params); }

QList<PAudioParams> GstAudioRecorderContext::preferences() const { return QList<PAudioParams>(); }

void GstAudioRecorderContext::control_recordData(const QByteArray &packet) { recorder.push_data_for_read(packet); }

void GstAudioRecorderContext::start()
{
    Q_ASSERT(!control && !isStarted);

    writeMutex.lock();

    control = new RwControlLocal(gstLoop, this);
    connect(control, &RwControlLocal::statusReady, this, &GstAudioRecorderContext::control_statusReady);
    connect(control, &RwControlLocal::audioInputIntensityChanged, this,
            &GstAudioRecorderContext::control_audioInputIntensityChanged);

    struct RecordDataCB {
        static void cb_control_recordData(const QByteArray &packet, void *app)
        {
            static_cast<GstAudioRecorderContext *>(app)->control_recordData(packet);
        }
    };

    control->app           = this;
    control->cb_recordData = &RecordDataCB::cb_control_recordData;

    allowWrites = true;
    writeMutex.unlock();

    // recorder.control = control;

    lastStatus    = RwControlStatus();
    isStarted     = false;
    pendingStatus = true;
    control->start(devices, RwControlConfigCodecs());
}

void GstAudioRecorderContext::pause() { }

void GstAudioRecorderContext::stop() { recorder.stop(); }

AudioRecorderContext::Error GstAudioRecorderContext::errorCode() const
{
    return static_cast<Error>(lastStatus.errorCode);
}

void GstAudioRecorderContext::control_statusReady(const RwControlStatus &status)
{
    lastStatus = status;

    if (status.finished) {
        // finished status just means the file is done
        //   sending.  the session still remains active.
        emit finished();
    } else if (status.error) {
        cleanup();
        emit error();
    } else if (pendingStatus) {
        if (status.stopped) {
            pendingStatus = false;

            cleanup();
            emit stopped();
            return;
        }

        // if we're currently stopping, ignore all other
        //   pending status events except for stopped
        //   (handled above)
        if (isStopping)
            return;

        pendingStatus = false;

        if (!isStarted) {
            isStarted = true;
            emit started();
        } else
            emit preferencesUpdated();
    }
}

void GstAudioRecorderContext::control_audioInputIntensityChanged(int intensity)
{
    emit audioInputIntensityChanged(intensity);
}

} // namespace PsiMedia
