// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
class NotepadNextApplication;
class MainWindow;
QJsonArray runWorkspaceUiChecks(NotepadNextApplication *app, MainWindow *window, const QString &output);
