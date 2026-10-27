// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>

class MainWindow;
class NotepadNextApplication;
class ScintillaNext;
class MarkdownPreview;

class WorkspaceFeatures final : public QObject
{
    Q_OBJECT
public:
    WorkspaceFeatures(MainWindow *window, NotepadNextApplication *app);
private:
    void watchEditor(ScintillaNext *editor);
    void showPreview(ScintillaNext *editor);
    void updatePreview();
    void showDevOpsAssistance();
    MainWindow *window;
    NotepadNextApplication *app;
    MarkdownPreview *preview;
    QPointer<ScintillaNext> previewEditor;
    QTimer debounce;
};
