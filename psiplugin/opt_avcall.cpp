#include "opt_avcall.h"

//#include "../avcall/avcall.h"
//#include "../avcall/mediadevicewatcher.h"
#include "../psimedia/psimedia.h"
//#include "common.h"
//#include "iconwidget.h"
//#include "psioptions.h"
#include "ui_opt_avcall.h"

#include <QComboBox>
#include <QLineEdit>
#include <QList>

class OptAvCallUI : public QWidget, public Ui::OptAvCall {
public:
    OptAvCallUI() : QWidget() { setupUi(this); }
};

//----------------------------------------------------------------------------
// OptionsTabAvCall
//----------------------------------------------------------------------------

OptionsTabAvCall::OptionsTabAvCall(QIcon icon) : _icon(icon)
{
    // connect(MediaDeviceWatcher::instance(), &MediaDeviceWatcher::updated, this, [this]() { restoreOptions(); });
}

OptionsTabAvCall::~OptionsTabAvCall() { }

QWidget *OptionsTabAvCall::widget()
{
    if (w)
        return nullptr;

    w = new OptAvCallUI();
    // OptAvCallUI *d = static_cast<OptAvCallUI *>(w);

    return w;
}

void OptionsTabAvCall::applyOptions()
{
    if (!w)
        return;

    OptAvCallUI *d = static_cast<OptAvCallUI *>(w.data());
    /*
        PsiOptions::instance()->setOption("options.media.devices.audio-output",
                                          d->cb_audioOutDevice->itemData(d->cb_audioOutDevice->currentIndex()).toString());
        PsiOptions::instance()->setOption("options.media.devices.audio-input",
                                          d->cb_audioInDevice->itemData(d->cb_audioInDevice->currentIndex()).toString());
        PsiOptions::instance()->setOption("options.media.devices.video-input",
                                          d->cb_videoInDevice->itemData(d->cb_videoInDevice->currentIndex()).toString());
        PsiOptions::instance()->setOption("options.media.video-support", d->cb_videoSupport->isChecked());

        MediaDeviceWatcher::instance()->updateDefaults();
        */
}

void OptionsTabAvCall::restoreOptions()
{
    if (!w)
        return;

    OptAvCallUI *d = static_cast<OptAvCallUI *>(w.data());
    /*
        auto dw = MediaDeviceWatcher::instance();
        d->cb_audioOutDevice->clear();
        if (dw->audioOutputDevices().isEmpty())
            d->cb_audioOutDevice->addItem("<None>", QString());
        for (const PsiMedia::Device &dev : dw->audioOutputDevices())
            d->cb_audioOutDevice->addItem(dev.name(), dev.id());

        d->cb_audioInDevice->clear();
        if (dw->audioInputDevices().isEmpty())
            d->cb_audioInDevice->addItem("<None>", QString());
        for (const PsiMedia::Device &dev : dw->audioInputDevices())
            d->cb_audioInDevice->addItem(dev.name(), dev.id());

        d->cb_videoInDevice->clear();
        if (dw->videoInputDevices().isEmpty())
            d->cb_videoInDevice->addItem("<None>", QString());
        for (const PsiMedia::Device &dev : dw->videoInputDevices())
            d->cb_videoInDevice->addItem(dev.name(), dev.id());

        auto config = dw->configuration();

        d->cb_audioOutDevice->setCurrentIndex(d->cb_audioOutDevice->findData(config.audioOutDeviceId));
        d->cb_audioInDevice->setCurrentIndex(d->cb_audioInDevice->findData(config.audioInDeviceId));
        d->cb_videoInDevice->setCurrentIndex(d->cb_videoInDevice->findData(config.videoInDeviceId));
        d->cb_videoSupport->setChecked(PsiOptions::instance()->getOption("options.media.video-support").toBool());
        */
}

QByteArray OptionsTabAvCall::id() const { return "avcall"; }

QByteArray OptionsTabAvCall::nextToId() const { return "sound"; }

QByteArray OptionsTabAvCall::parentId() const { return ""; }

QString OptionsTabAvCall::title() const { return QObject::tr("Voice Calling"); }

QIcon OptionsTabAvCall::icon() const { return _icon; }

QString OptionsTabAvCall::desc() const { return QObject::tr("Audio and video device configuration"); }

void OptionsTabAvCall::setCallbacks(std::function<void()> dataChanged, std::function<void(bool)> noDirty,
                                    std::function<void(QWidget *)> connectDataChanged)
{
}
