// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ExternalTerminals.h"
#include <functional>
class QMenu;
class QSettings;
class QWidget;

using ExternalTerminalRequest = std::function<void(const ExternalTerminals::Terminal &, const QString &)>;
// Separating menu construction from launch keeps right-click handling testable
// without opening an actual terminal or introducing an embedded terminal host.
void populateOpenShellMenu(QMenu *menu, const QString &directory,
    const ExternalTerminals::Settings &settings, const QList<ExternalTerminals::Terminal> &available,
    const ExternalTerminalRequest &request, const std::function<void()> &configure);
QMenu *addOpenShellMenu(QMenu *parent, const QString &directory, QSettings *settings,
    QWidget *errorParent, const std::function<void()> &configure);
void openDefaultExternalTerminal(QSettings *settings, const QString &directory, QWidget *parent);
