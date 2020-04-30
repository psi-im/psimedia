/*
 * extendedoptionsplugin.cpp - plugin
 * Copyright (C) 2010-2014  Evgeny Khryukin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "psiplugin.h"
#include "applicationinfoaccessinghost.h"
#include "applicationinfoaccessor.h"
#include "gstprovider.h"
#include "iconfactoryaccessinghost.h"
#include "iconfactoryaccessor.h"
#include "opt_avcall.h"
#include "optionaccessinghost.h"
#include "optionaccessor.h"
#include "plugininfoprovider.h"
#include "psimediaprovider.h"

#include <QIcon>

#define constVersion "0.1"

class PsiMediaPlugin : public QObject,
                       public PsiPlugin,
                       public OptionAccessor,
                       public ApplicationInfoAccessor,
                       public IconFactoryAccessor,
                       public PluginInfoProvider,
                       public PsiMedia::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.psi-im.PsiMediaPlugin")
    Q_INTERFACES(
        PsiPlugin OptionAccessor ApplicationInfoAccessor PluginInfoProvider IconFactoryAccessor PsiMedia::Plugin)

public:
    PsiMediaPlugin() = default;
    QString             name() const override;
    QString             shortName() const override;
    QString             version() const override;
    QWidget *           options() override;
    bool                enable() override;
    bool                disable() override;
    void                optionChanged(const QString &option) override;
    void                applyOptions() override;
    void                restoreOptions() override;
    void                setOptionAccessingHost(OptionAccessingHost *host) override;
    void                setApplicationInfoAccessingHost(ApplicationInfoAccessingHost *host) override;
    void                setIconFactoryAccessingHost(IconFactoryAccessingHost *host) override;
    QString             pluginInfo() override;
    QPixmap             icon() const override;
    PsiMedia::Provider *createProvider(const QVariantMap &) override;

private:
    OptionAccessingHost *         psiOptions = nullptr;
    IconFactoryAccessingHost *    iconHost   = nullptr;
    ApplicationInfoAccessingHost *appInfo    = nullptr;
    bool                          enabled    = false;
    QPointer<QWidget>             options_;

    OAH_PluginOptionsTab * tab      = nullptr;
    PsiMedia::GstProvider *provider = nullptr;
};

QString PsiMediaPlugin::name() const { return "Psi Multimedia"; }

QString PsiMediaPlugin::shortName() const { return "psimedia"; }

QString PsiMediaPlugin::version() const { return constVersion; }

bool PsiMediaPlugin::enable()
{
    if (!psiOptions)
        return false;
    enabled = true;

    if (!provider) {
        provider = new PsiMedia::GstProvider();
        provider->init();
    }
    if (!tab)
        tab = new OptionsTabAvCall(provider, psiOptions, icon());
    psiOptions->addSettingPage(tab);

    return enabled;
}

bool PsiMediaPlugin::disable()
{
    if (!enabled)
        return true;

    if (tab)
        psiOptions->removeSettingPage(tab);
    delete tab;
    tab = nullptr;

    if (provider)
        delete provider;
    provider = nullptr;

    enabled = false;
    return true;
}

QWidget *PsiMediaPlugin::options() { return nullptr; }

void PsiMediaPlugin::applyOptions() { return; }

void PsiMediaPlugin::restoreOptions() { return; }

void PsiMediaPlugin::setOptionAccessingHost(OptionAccessingHost *host) { psiOptions = host; }
void PsiMediaPlugin::setIconFactoryAccessingHost(IconFactoryAccessingHost *host) { iconHost = host; }
void PsiMediaPlugin::setApplicationInfoAccessingHost(ApplicationInfoAccessingHost *host) { appInfo = host; }

void PsiMediaPlugin::optionChanged(const QString &option) { Q_UNUSED(option); }

QString PsiMediaPlugin::pluginInfo()
{
    return tr("Authors: ")
        + "\n  Justing Kerneges (Barracuda Networks)\n"
          "  Sergey Ilinykh <rion4ik@gmail.com>\n"
        + tr("Thanks To")
        + ":\n"
          "  Vitaly Tonkacheyev <thetvg@gmail.com>\n\n"
        + tr("Media plugin provides functionality required for Audio/Video calls and can also replace some parts of "
             "QtMultimedia.");
}

QPixmap PsiMediaPlugin::icon() const { return QPixmap(":/icons/avcall.png"); }

PsiMedia::Provider *PsiMediaPlugin::createProvider(const QVariantMap &)
{
    // We don't need more than one provider in Psi
    return provider;
}

#include "psiplugin.moc"
