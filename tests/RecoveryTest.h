// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
class NotepadNextApplication;
class MainWindow;
// Runs only from the isolated application smoke test. Never uses the real profile.
QJsonArray runRecoveryChecks(NotepadNextApplication *app, MainWindow *window);
