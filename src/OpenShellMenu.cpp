// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenShellMenu.h"
#include "UiIcons.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("OpenShellMenu", text); }
void launch(const ExternalTerminals::Terminal &terminal, const QString &directory, QWidget *parent)
{
    const auto result = ExternalTerminals::launch(ExternalTerminals::buildLaunch(terminal, directory));
    if (!result.ok) QMessageBox::warning(parent, tr("Could not open shell"), result.error);
}
}

void populateOpenShellMenu(QMenu *menu, const QString &directory,
    const ExternalTerminals::Settings &settings, const QList<ExternalTerminals::Terminal> &available,
    const ExternalTerminalRequest &request, const std::function<void()> &configure)
{
    menu->clear();
    const auto preferred = ExternalTerminals::terminalForSettings(settings, available);
    auto *defaultAction = menu->addAction(uiIcon(UiIcon::Terminal, menu), preferred.program.isEmpty()
        ? tr("Default terminal unavailable") : tr("Default: %1").arg(preferred.name));
    defaultAction->setObjectName(QStringLiteral("openShellDefault"));
    defaultAction->setEnabled(!preferred.program.isEmpty() && QFileInfo(directory).isDir());
    defaultAction->setToolTip(preferred.program.isEmpty()
        ? tr("Choose an installed terminal in Terminal preferences.") : preferred.program);
    QObject::connect(defaultAction, &QAction::triggered, menu, [request, preferred, directory] { request(preferred, directory); });
    menu->addSeparator();
    auto choices = available;
    if (!settings.customProgram.trimmed().isEmpty()) {
        auto custom = settings;
        custom.terminalId = QStringLiteral("custom");
        choices.append(ExternalTerminals::terminalForSettings(custom, available));
    }
    for (const auto &terminal : choices) {
        auto *action = menu->addAction(terminal.name);
        action->setObjectName(QStringLiteral("openShellChoice"));
        action->setData(terminal.id);
        action->setCheckable(true);
        action->setChecked(terminal.id == preferred.id);
        action->setToolTip(terminal.program);
        action->setEnabled(!terminal.program.isEmpty() && QFileInfo(directory).isDir());
        QObject::connect(action, &QAction::triggered, menu, [request, terminal, directory] { request(terminal, directory); });
    }
    if (choices.isEmpty()) {
        auto *empty = menu->addAction(tr("No terminal applications found"));
        empty->setEnabled(false);
    }
    menu->addSeparator();
    auto *settingsAction = menu->addAction(uiIcon(UiIcon::Settings, menu), tr("Configure terminals…"));
    settingsAction->setObjectName(QStringLiteral("configureTerminals"));
    QObject::connect(settingsAction, &QAction::triggered, menu, [configure] { if (configure) configure(); });
}

QMenu *addOpenShellMenu(QMenu *parent, const QString &directory, QSettings *settings,
    QWidget *errorParent, const std::function<void()> &configure)
{
    auto *menu = parent->addMenu(uiIcon(UiIcon::Terminal, parent), tr("Open Shell"));
    menu->setObjectName(QStringLiteral("openShellMenu"));
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, directory, settings, errorParent, configure] {
        populateOpenShellMenu(menu, directory, ExternalTerminals::readSettings(settings), ExternalTerminals::discover(),
            [errorParent](const auto &terminal, const auto &folder) { launch(terminal, folder, errorParent); }, configure);
    });
    return menu;
}

void openDefaultExternalTerminal(QSettings *settings, const QString &directory, QWidget *parent)
{
    const auto result = ExternalTerminals::open(ExternalTerminals::readSettings(settings), directory);
    if (!result.ok) QMessageBox::warning(parent, tr("Could not open shell"), result.error);
}
