// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class MainWindow;
class NotepadNextApplication;
class ScintillaNext;

void installCompare(MainWindow *window, NotepadNextApplication *app);
void toggleCompareSelection(ScintillaNext *editor);
void prepareCompareMenu(ScintillaNext *editor);
