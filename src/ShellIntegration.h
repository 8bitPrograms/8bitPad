// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

class QWidget;

QString windowsOpenCommand(const QString &executable, const QString &argumentToken);
QString linuxDesktopEntry(const QString &executable);
QString mimeTypeForExtension(const QString &extension);

void installShellIntegration(class QWidget *window);
void showShellIntegrationDialog(QWidget *parent);
