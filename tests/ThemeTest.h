// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonArray>

class MainWindow;
class NotepadNextApplication;

// Uses the real application/editor/Lua stack. Compile into the application only
// with BUILD_TESTING and call from the isolated --smoke-test path.
QJsonArray runThemeChecks(NotepadNextApplication *app, MainWindow *window);
