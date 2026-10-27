/*
 * This file is part of Notepad Next.
 * Copyright 2022 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "GitStatus.h"

#include <QColor>
#include <QDockWidget>
#include <QStringList>

namespace Ui {
class FolderAsWorkspaceDock;
}

class QFileSystemModel;
class GitFileProxy;

class FolderAsWorkspaceDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit FolderAsWorkspaceDock(QWidget *parent = nullptr);
    ~FolderAsWorkspaceDock();

    void setRootPath(const QString &dir, bool showPanel = true);
    QString rootPath() const;
    bool hasFolder() const;
    void closeFolder();
    void applyFolderVisibility();
    void refreshGit();
    void setGitColors(const QColor &modified, const QColor &added, const QColor &untracked, const QColor &deleted);

signals:
    void fileDoubleClicked(const QString &filePath);
    void gitStatusChanged(const QString &label, const QString &toolTip);
    void pathRenamed(const QString &oldPath, const QString &newPath);
    void markdownPreviewRequested(const QString &path);
    void terminalSettingsRequested();

private:
    void reloadTree();
    void refreshFolder();
    void showContextMenu(const QPoint &position);
    void createItem(const QString &directory, bool folder);
    void renameSelected();
    void copySelection(bool cut);
    void pasteInto(const QString &directory);
    void transferPaths(const QStringList &paths, const QString &directory, bool move);
    void trashSelection();
    void revealPath(const QString &path);
    QString currentDirectory() const;
    bool hasExplorerClipboard() const;
    void updateClipboard();

    Ui::FolderAsWorkspaceDock *ui;
    QFileSystemModel *model;
    GitFileProxy *proxy = nullptr;
    GitMonitor *git = nullptr;
    QString mRoot;
    QStringList clipboardPaths;
    QString clipboardToken;
    bool clipboardCut = false;
    bool copyBusy = false;
};
