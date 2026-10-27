// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>

class MainWindow;
class NotepadNextApplication;
class ScintillaNext;
class QLabel;
class QAction;

class BitPadFeatures final : public QObject
{
    Q_OBJECT
public:
    BitPadFeatures(MainWindow *window, NotepadNextApplication *app);
private:
    void attachEditor(ScintillaNext *editor);
    void updateMode();
    void showCommandPalette();
    MainWindow *window;
    NotepadNextApplication *app;
    QLabel *modeLabel;
    QLabel *promptLabel;
    QAction *vimAction;
};
