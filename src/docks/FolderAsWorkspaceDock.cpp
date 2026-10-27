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


#include "FolderAsWorkspaceDock.h"
#include "ApplicationSettings.h"
#include "ExplorerFileOperations.h"
#include "ExplorerFileTypeIcon.h"
#include "OpenShellMenu.h"
#include "ui_FolderAsWorkspaceDock.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QIdentityProxyModel>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QTimer>
#include <QToolButton>
#include <QMenu>
#include <QUrl>
#include <QUuid>
#include <QtConcurrentRun>
#include <atomic>
#include <memory>

class GitFileProxy final : public QIdentityProxyModel
{
public:
    explicit GitFileProxy(QFileSystemModel *files, QObject *parent = nullptr)
        : QIdentityProxyModel(parent), files(files)
    {
        setSourceModel(files);
    }

    QHash<QString, GitPathState> states;
    QColor modified = QColor(196, 64, 64);
    QColor added = QColor(46, 140, 78);
    QColor untracked = QColor(40, 110, 180);
    QColor deleted = QColor(120, 120, 120);
    QFileSystemModel *files = nullptr;

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.column() == 0 && files && role == Qt::DecorationRole)
            return explorerFileTypeIcon(files->fileInfo(mapToSource(index)));
        if (index.column() == 0 && files && (role == Qt::ForegroundRole || role == Qt::ToolTipRole)) {
            const QString path = normalizePath(files->filePath(mapToSource(index)));
            if (states.contains(path)) {
                const GitPathState state = states.value(path);
                if (role == Qt::ToolTipRole) {
                    const char *name = "Changed";
                    if (state == GitPathState::Added) name = "Added";
                    else if (state == GitPathState::Untracked) name = "Untracked";
                    else if (state == GitPathState::Deleted) name = "Deleted";
                    return files->filePath(mapToSource(index)) + QStringLiteral("\n") + QString::fromLatin1(name);
                }
                if (state == GitPathState::Added) return added;
                if (state == GitPathState::Untracked) return untracked;
                if (state == GitPathState::Deleted) return deleted;
                return modified;
            }
        }
        return QIdentityProxyModel::data(index, role);
    }
};

ApplicationSetting<QString> rootPathSetting{"FolderAsWorkspace/RootPath"};

namespace {
    // QFileSystemModel's standard column order.
    enum FileSystemColumn
    {
        NameColumn = 0,
        SizeColumn,
        TypeColumn,
        DateColumn
    };
}

FolderAsWorkspaceDock::FolderAsWorkspaceDock(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::FolderAsWorkspaceDock),
    model(new QFileSystemModel(this)),
    git(new GitMonitor(this))
{
    ui->setupUi(this);
    setWindowTitle(tr("Folder"));
    toggleViewAction()->setText(tr("Folder"));

    model->setOption(QFileSystemModel::DontWatchForChanges, false);
    model->setOption(QFileSystemModel::DontResolveSymlinks, true);
    model->setOption(QFileSystemModel::DontUseCustomDirectoryIcons, true);
    proxy = new GitFileProxy(model, this);
    ui->treeView->setModel(proxy);
    ui->treeView->pathForIndex = [this](const QModelIndex &index) {
        return model->filePath(proxy->mapToSource(index));
    };
    ui->treeView->workspacePath = [this] { return mRoot; };
    ui->treeView->transferRequested = [this](const QStringList &paths, const QString &directory, bool move) {
        transferPaths(paths, directory, move);
    };

    QMenu *menu = new QMenu(ui->toolButton);
    menu->addAction(ui->actionShowSize);
    menu->addAction(ui->actionShowType);
    menu->addAction(ui->actionShowDateModified);
    menu->addSeparator();
    menu->addAction(ui->actionShowHidden);

    ui->toolButton->setMenu(menu);
    ui->toolButton->setPopupMode(QToolButton::InstantPopup);
    ui->toolButton->setToolTip(tr("Folder options"));
    ui->toolButton->setAutoRaise(true);

    auto bindColumnVisibility = [this](QAction* action, int column) {
        ui->treeView->setColumnHidden(column, !action->isChecked());
        connect(action, &QAction::toggled, this, [this, column](bool checked) {
            ui->treeView->setColumnHidden(column, !checked);
        });
    };

    auto bindActionToSetting = [](QAction* action, const QString& key) {
        ApplicationSettings settings;

        action->setChecked(settings.value(key, false).toBool());
        connect(action, &QAction::toggled, [key](bool checked) {
            ApplicationSettings settings;
            settings.setValue(key, checked);
        });
    };

    auto bindColumn = [&](QAction* action, int column, const QString& key) {
        bindColumnVisibility(action, column);
        bindActionToSetting(action, key);
    };

    bindColumn(ui->actionShowSize, SizeColumn, "FolderAsWorkspace/ShowSize");
    bindColumn(ui->actionShowType, TypeColumn, "FolderAsWorkspace/ShowType");
    bindColumn(ui->actionShowDateModified, DateColumn, "FolderAsWorkspace/ShowDateModified");

    connect(ui->actionShowHidden, &QAction::toggled, this, [this](bool showHidden) {
        auto filters = QDir::AllEntries | QDir::AllDirs | QDir::NoDotAndDotDot;
        if (showHidden) filters |= QDir::Hidden;
        model->setFilter(filters);
    });
    bindActionToSetting(ui->actionShowHidden, "FolderAsWorkspace/ShowHidden");
    auto filters = QDir::AllEntries | QDir::AllDirs | QDir::NoDotAndDotDot;
    if (ui->actionShowHidden->isChecked()) filters |= QDir::Hidden;
    model->setFilter(filters);

    // Default sorting
    ui->treeView->sortByColumn(0, Qt::AscendingOrder);

    connect(ui->treeView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QFileInfo info = model->fileInfo(proxy->mapToSource(index));
        if (info.isFile())
            emit fileDoubleClicked(info.absoluteFilePath());
    });
    ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeView, &QWidget::customContextMenuRequested, this, &FolderAsWorkspaceDock::showContextMenu);
    // Claim these only while the explorer has focus. The application's editor
    // actions use the same keys and must not receive file-management shortcuts.
    ui->treeView->handleShortcut = [this](QKeyEvent *event, bool probe) {
        if (event->matches(QKeySequence::Copy)) { if (!probe) copySelection(false); }
        else if (event->matches(QKeySequence::Cut)) { if (!probe) copySelection(true); }
        else if (event->matches(QKeySequence::Paste)) { if (!probe) pasteInto(currentDirectory()); }
        else if (event->matches(QKeySequence::Delete)) { if (!probe) trashSelection(); }
        else if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier) { if (!probe) renameSelected(); }
        else if (event->key() == Qt::Key_F5 && event->modifiers() == Qt::NoModifier) { if (!probe) refreshFolder(); }
        else if (event->key() == Qt::Key_N && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (!probe) createItem(currentDirectory(), true);
        } else return false;
        return true;
    };

    auto *closeButton = new QToolButton(this);
    closeButton->setText(tr("Close"));
    closeButton->setToolTip(tr("Close folder"));
    ui->horizontalLayout->insertWidget(0, closeButton);
    connect(closeButton, &QToolButton::clicked, this, &FolderAsWorkspaceDock::closeFolder);
    auto *refresh = menu->addAction(tr("Refresh"));
    connect(refresh, &QAction::triggered, this, &FolderAsWorkspaceDock::refreshFolder);
    connect(git, &GitMonitor::updated, this, [this](const GitSnapshot &snapshot) {
        proxy->states = snapshot.paths;
        ui->treeView->viewport()->update();
        emit gitStatusChanged(snapshot.label(), snapshot.toolTip());
    });

    // Configure header settings
    QHeaderView *header = ui->treeView->header();
    header->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(SizeColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(TypeColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(DateColumn, QHeaderView::ResizeToContents);

    ApplicationSettings settings;
    const QString saved = settings.get(rootPathSetting);
    if (QFileInfo(saved).isDir())
        setRootPath(saved, false);
    applyFolderVisibility();
}

FolderAsWorkspaceDock::~FolderAsWorkspaceDock()
{
    delete ui;
}

void FolderAsWorkspaceDock::setRootPath(const QString &dir, bool showPanel)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return;
    mRoot = QFileInfo(dir).canonicalFilePath();
    ApplicationSettings settings;
    settings.set(rootPathSetting, mRoot);
    reloadTree();
    applyFolderVisibility();
    if (showPanel) {
        show();
        raise();
    }
    git->watch(mRoot);
}

void FolderAsWorkspaceDock::closeFolder()
{
    mRoot.clear();
    ApplicationSettings settings;
    settings.set(rootPathSetting, QString());
    proxy->states.clear();
    model->setRootPath(QString());
    ui->treeView->setRootIndex(QModelIndex());
    git->watch(QString());
    hide();
    applyFolderVisibility();
    emit gitStatusChanged(QString(), QString());
}

void FolderAsWorkspaceDock::applyFolderVisibility()
{
    toggleViewAction()->setVisible(hasFolder());
    if (!hasFolder())
        hide();
}

void FolderAsWorkspaceDock::refreshGit()
{
    if (hasFolder())
        git->schedule();
}

void FolderAsWorkspaceDock::setGitColors(const QColor &modified, const QColor &added, const QColor &untracked, const QColor &deleted)
{
    proxy->modified = modified;
    proxy->added = added;
    proxy->untracked = untracked;
    proxy->deleted = deleted;
    ui->treeView->viewport()->update();
}

void FolderAsWorkspaceDock::reloadTree()
{
    if (!hasFolder())
        return;
    const QModelIndex sourceIndex = model->setRootPath(mRoot);
    ui->treeView->setRootIndex(proxy->mapFromSource(sourceIndex));
}

QString FolderAsWorkspaceDock::rootPath() const
{
    return mRoot;
}

bool FolderAsWorkspaceDock::hasFolder() const
{
    return !mRoot.isEmpty() && QFileInfo(mRoot).isDir();
}

QString FolderAsWorkspaceDock::currentDirectory() const
{
    const QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) return mRoot;
    const QFileInfo info(model->filePath(proxy->mapToSource(index)));
    return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
}

void FolderAsWorkspaceDock::showContextMenu(const QPoint &position)
{
    if (!hasFolder() || copyBusy) return;
    const QModelIndex index = ui->treeView->indexAt(position);
    if (index.isValid()) {
        if (!ui->treeView->selectionModel()->isSelected(index))
            ui->treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        else
            ui->treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    } else {
        ui->treeView->clearSelection();
        ui->treeView->setCurrentIndex(QModelIndex());
    }
    const QStringList paths = ui->treeView->selectedPaths();
    const QString directory = currentDirectory();
    const QFileInfo info(paths.size() == 1 ? paths.first() : QString());
    QMenu menu(this);
    ApplicationSettings terminalSettings;
    addOpenShellMenu(&menu, directory, &terminalSettings, this, [this] { emit terminalSettingsRequested(); });
    menu.addSeparator();
    if (paths.size() == 1 && info.isFile()) {
        menu.addAction(tr("Open"), this, [this, path = paths.first()] { emit fileDoubleClicked(path); });
        const QString suffix = info.suffix().toLower();
        if (suffix == "md" || suffix == "markdown" || suffix == "mdown")
            menu.addAction(tr("Markdown Preview"), this, [this, path = paths.first()] { emit markdownPreviewRequested(path); });
        menu.addSeparator();
    }
    menu.addAction(explorerFileTypeIcon(QFileInfo("new.txt")), tr("New File…"), this,
                   [this, directory] { createItem(directory, false); });
    menu.addAction(explorerFileTypeIcon(QFileInfo(directory)), tr("New Folder…"), this,
                   [this, directory] { createItem(directory, true); });
    menu.addSeparator();
    auto *cut = menu.addAction(tr("Cut"), this, [this] { copySelection(true); });
    cut->setShortcut(QKeySequence::Cut);
    cut->setEnabled(!paths.isEmpty());
    auto *copy = menu.addAction(tr("Copy"), this, [this] { copySelection(false); });
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(!paths.isEmpty());
    auto *paste = menu.addAction(tr("Paste"), this, [this, directory] { pasteInto(directory); });
    paste->setShortcut(QKeySequence::Paste);
    paste->setEnabled(hasExplorerClipboard());
    if (!paths.isEmpty()) {
        menu.addAction(tr("Move to Folder…"), this, [this, paths, directory] {
            const QString target = QFileDialog::getExistingDirectory(this, tr("Move to a folder in this workspace"), directory);
            if (!target.isEmpty()) transferPaths(paths, target, true);
        });
        auto *rename = menu.addAction(tr("Rename…"), this, &FolderAsWorkspaceDock::renameSelected);
        rename->setShortcut(QKeySequence(Qt::Key_F2));
        rename->setEnabled(paths.size() == 1);
        auto *trash = menu.addAction(tr("Move to Recycle Bin / Trash"), this, &FolderAsWorkspaceDock::trashSelection);
        trash->setShortcut(QKeySequence::Delete);
    }
    menu.addSeparator();
    const QStringList pathTargets = paths.isEmpty() ? QStringList{mRoot} : paths;
    menu.addAction(tr("Copy Path"), this, [pathTargets] {
        QStringList native;
        for (const QString &path : pathTargets) native << QDir::toNativeSeparators(path);
        QApplication::clipboard()->setText(native.join('\n'));
    });
    menu.addAction(tr("Copy Relative Path"), this, [this, pathTargets] {
        QStringList relative;
        for (const QString &path : pathTargets) relative << QDir(mRoot).relativeFilePath(path);
        QApplication::clipboard()->setText(relative.join('\n'));
    });
    menu.addAction(tr("Open Containing Folder"), this, [this, paths] {
        const QString path = paths.isEmpty() ? mRoot : QFileInfo(paths.first()).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    if (paths.size() == 1 && info.isDir())
        menu.addAction(tr("Open This Folder as Workspace"), this, [this, path = paths.first()] { setRootPath(path); });
    auto *refresh = menu.addAction(tr("Refresh"), this, &FolderAsWorkspaceDock::refreshFolder);
    refresh->setShortcut(QKeySequence(Qt::Key_F5));
    menu.exec(ui->treeView->viewport()->mapToGlobal(position));
}

void FolderAsWorkspaceDock::createItem(const QString &directory, bool folder)
{
    if (!hasFolder() || copyBusy) return;
    QString name;
    while (true) {
        bool accepted = false;
        name = QInputDialog::getText(this, folder ? tr("New Folder") : tr("New File"),
            folder ? tr("Folder name:") : tr("File name, including extension (for example, notes.md):"),
            QLineEdit::Normal, name, &accepted);
        if (!accepted) return;
        const Explorer::FileOperations operations(mRoot);
        const auto result = folder ? operations.createDirectory(directory, name) : operations.createFile(directory, name);
        if (!result.ok) {
            QMessageBox::warning(this, tr("Could Not Create Item"), result.error);
            continue;
        }
        revealPath(result.path);
        refreshGit();
        if (!folder) emit fileDoubleClicked(result.path);
        return;
    }
}

void FolderAsWorkspaceDock::renameSelected()
{
    if (copyBusy) return;
    const QStringList paths = ui->treeView->selectedPaths();
    if (paths.size() != 1) return;
    const QString oldPath = paths.first();
    QString name = QFileInfo(oldPath).fileName();
    while (true) {
        bool accepted = false;
        name = QInputDialog::getText(this, tr("Rename"), tr("New name, including extension:"),
                                     QLineEdit::Normal, name, &accepted);
        if (!accepted || name == QFileInfo(oldPath).fileName()) return;
        const auto result = Explorer::FileOperations(mRoot).rename(oldPath, name);
        if (!result.ok) {
            if (!result.path.isEmpty()) {
                emit pathRenamed(oldPath, result.path);
                revealPath(result.path);
                refreshGit();
                QMessageBox::warning(this, tr("Could Not Rename Item"), result.error);
                return;
            }
            QMessageBox::warning(this, tr("Could Not Rename Item"), result.error);
            continue;
        }
        // Update documents before queued filesystem-watch notifications run.
        emit pathRenamed(oldPath, result.path);
        revealPath(result.path);
        refreshGit();
        return;
    }
}

namespace {
constexpr auto clipboardFormat = "application/x-8bitpad-workspace-files";
}

bool FolderAsWorkspaceDock::hasExplorerClipboard() const
{
    const auto *mime = QApplication::clipboard()->mimeData();
    return mime && !clipboardPaths.isEmpty() && !clipboardToken.isEmpty()
        && mime->data(clipboardFormat) == clipboardToken.toUtf8();
}

void FolderAsWorkspaceDock::updateClipboard()
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : clipboardPaths) urls << QUrl::fromLocalFile(path);
    mime->setUrls(urls);
    mime->setData(clipboardFormat, clipboardToken.toUtf8());
    QApplication::clipboard()->setMimeData(mime);
}

void FolderAsWorkspaceDock::copySelection(bool cut)
{
    if (copyBusy) return;
    const QStringList paths = ui->treeView->selectedPaths();
    if (paths.isEmpty()) return;
    clipboardPaths = paths;
    clipboardCut = cut;
    clipboardToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    updateClipboard();
}

void FolderAsWorkspaceDock::pasteInto(const QString &directory)
{
    if (hasExplorerClipboard())
        transferPaths(clipboardPaths, directory, clipboardCut);
}

void FolderAsWorkspaceDock::transferPaths(const QStringList &paths, const QString &directory, bool move)
{
    if (copyBusy || paths.isEmpty() || !hasFolder()) return;
    // Paste may pass clipboardPaths itself; successful moves update that list.
    const QStringList sources = paths;
    const Explorer::FileOperations operations(mRoot);
    if (move) {
        QStringList errors;
        QString lastPath;
        const bool ownsClipboard = hasExplorerClipboard();
        // Keep these operations and path notifications in the same event-loop
        // turn so dirty open documents follow a directory move immediately.
        for (const QString &path : sources) {
            const auto result = operations.move(path, directory);
            if (!result.ok) {
                errors << QFileInfo(path).fileName() + ": " + result.error;
                continue;
            }
            if (path != result.path) emit pathRenamed(path, result.path);
            lastPath = result.path;
            if (ownsClipboard && clipboardCut) clipboardPaths.removeAll(path);
        }
        if (ownsClipboard && clipboardCut) updateClipboard();
        if (!lastPath.isEmpty()) revealPath(lastPath);
        refreshGit();
        if (!errors.isEmpty()) QMessageBox::warning(this, tr("Some Items Could Not Be Moved"), errors.join("\n\n"));
        return;
    }

    copyBusy = true;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    const QMetaObject::Connection cancelOnDestroy = connect(this, &QObject::destroyed, [cancelled] {
        cancelled->store(true);
    });
    auto *progress = new QProgressDialog(tr("Copying files…"), tr("Cancel"), 0, 0, this);
    progress->setWindowTitle(tr("Copy Files"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(250);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    connect(progress, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
    using Results = QList<QPair<QString, Explorer::OperationResult>>;
    auto *watcher = new QFutureWatcher<Results>(this);
    connect(watcher, &QFutureWatcher<Results>::finished, this, [this, watcher, progress, cancelOnDestroy] {
        disconnect(cancelOnDestroy);
        const auto results = watcher->result();
        copyBusy = false;
        progress->close();
        progress->deleteLater();
        watcher->deleteLater();
        QStringList errors;
        QString lastPath;
        for (const auto &entry : results) {
            if (entry.second.ok) lastPath = entry.second.path;
            else if (!entry.second.cancelled) errors << QFileInfo(entry.first).fileName() + ": " + entry.second.error;
        }
        if (!lastPath.isEmpty()) revealPath(lastPath);
        refreshGit();
        if (!errors.isEmpty()) QMessageBox::warning(this, tr("Some Items Could Not Be Copied"), errors.join("\n\n"));
    });
    watcher->setFuture(QtConcurrent::run([operations, sources, directory, cancelled] {
        Results results;
        for (const QString &path : sources) {
            if (cancelled->load()) break;
            auto result = operations.copy(path, directory, [cancelled] { return cancelled->load(); });
            results.append({path, result});
            if (result.cancelled) break;
        }
        return results;
    }));
}

void FolderAsWorkspaceDock::trashSelection()
{
    if (copyBusy) return;
    const QStringList paths = ui->treeView->selectedPaths();
    if (paths.isEmpty()) return;
    const QString question = paths.size() == 1
        ? tr("Move \"%1\" to the Recycle Bin / Trash?").arg(QFileInfo(paths.first()).fileName())
        : tr("Move these %1 items to the Recycle Bin / Trash?").arg(paths.size());
    if (QMessageBox::question(this, tr("Move to Recycle Bin / Trash"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QStringList errors;
    const Explorer::FileOperations operations(mRoot);
    for (const QString &path : paths) {
        const auto result = operations.trash(path);
        if (!result.ok) errors << QFileInfo(path).fileName() + ": " + result.error;
    }
    refreshGit();
    if (!errors.isEmpty()) QMessageBox::warning(this, tr("Some Items Could Not Be Trashed"), errors.join("\n\n"));
}

void FolderAsWorkspaceDock::revealPath(const QString &path)
{
    // QFileSystemModel indexes the new entry asynchronously. Allow its watcher
    // to catch up, without resetting existing folder expansion or selection.
    auto select = [this, path] {
        const QModelIndex index = proxy->mapFromSource(model->index(path));
        if (!index.isValid()) return;
        ui->treeView->setCurrentIndex(index);
        ui->treeView->scrollTo(index);
    };
    select();
    QTimer::singleShot(150, this, select);
}

void FolderAsWorkspaceDock::refreshFolder()
{
    if (!hasFolder() || copyBusy) return;
    const QString selected = model->filePath(proxy->mapToSource(ui->treeView->currentIndex()));
    model->setRootPath(QString());
    reloadTree();
    if (!selected.isEmpty() && QFileInfo::exists(selected)) revealPath(selected);
    refreshGit();
}
