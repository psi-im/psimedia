#ifndef OPT_AVCALL_H
#define OPT_AVCALL_H

#include "optionaccessinghost.h"

#include <QIcon>

class OptionsTabAvCall : public OAH_PluginOptionsTab {
public:
    OptionsTabAvCall(QIcon icon);
    ~OptionsTabAvCall();

    QWidget *widget() override;

    void applyOptions() override;
    void restoreOptions() override;

    QByteArray id() const override;       // Unique identifier, i.e. "plugins_misha's_cool-plugin"
    QByteArray nextToId() const override; // the page will be added after this page
    QByteArray parentId() const override; // Identifier of parent tab, i.e. "general"

    QString title() const override; // "General"
    QIcon   icon() const override;
    QString desc() const override; // "You can configure your roster here"

    void setCallbacks(std::function<void()> dataChanged, std::function<void(bool)> noDirty,
                      std::function<void(QWidget *)> connectDataChanged) override;

private:
    QPointer<QWidget> w;
    QIcon             _icon;
};

#endif // OPT_AVCALL_H
