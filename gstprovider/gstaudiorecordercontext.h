#ifndef PSIMEDIA_GSTAUDIORECORDERCONTEXT_H
#define PSIMEDIA_GSTAUDIORECORDERCONTEXT_H

#include "gstrecorder.h"
#include "psimediaprovider.h"
#include "rwcontrol.h"

#include <QMutex>

namespace PsiMedia {

class GstMainLoop;

class GstAudioRecorderContext : public QObject, public AudioRecorderContext {
    Q_OBJECT
    Q_INTERFACES(PsiMedia::AudioRecorderContext)

    void cleanup();

    void control_recordData(const QByteArray &packet);

public:
    GstMainLoop *          gstLoop = nullptr;
    RwControlLocal *       control = nullptr;
    QMutex                 writeMutex;
    RwControlConfigDevices devices;
    RwControlStatus        lastStatus;
    GstRecorder            recorder;

    bool isStarted     = false;
    bool isStopping    = false;
    bool pendingStatus = false;
    bool allowWrites   = false;

    explicit GstAudioRecorderContext(GstMainLoop *_gstLoop, QObject *parent = nullptr);
    ~GstAudioRecorderContext() override;

    QObject *qobject() override;

    // AudioRecorderContext interface
public:
    void                setInputDevice(const QString &deviceId) override;
    void                setOutputDevice(QIODevice *recordDevice) override;
    void                setPreferences(const QList<PAudioParams> &params) override;
    QList<PAudioParams> preferences() const override;
    void                start() override;
    void                pause() override;
    void                stop() override;
    Error               errorCode() const override;

signals:
    void started();
    void preferencesUpdated();
    void audioOutputIntensityChanged(int intensity);
    void audioInputIntensityChanged(int intensity);
    void stopped();
    void finished();
    void error();

private slots:
    void control_statusReady(const RwControlStatus &status);
    void control_audioInputIntensityChanged(int intensity);
};

} // namespace PsiMedia

#endif // PSIMEDIA_GSTAUDIORECORDERCONTEXT_H
