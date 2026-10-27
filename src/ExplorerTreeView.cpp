// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExplorerTreeView.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QKeyEvent>
#include <QUrl>
#include <algorithm>

ExplorerTreeView::ExplorerTreeView(QWidget *parent) : QTreeView(parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setIconSize(QSize(16, 16));
    setAutoScroll(true);
}

QStringList ExplorerTreeView::selectedPaths() const
{
    QStringList paths;
    if (!selectionModel() || !pathForIndex)
        return paths;
    for (const auto &index : selectionModel()->selectedRows()) {
        const QString path = pathForIndex(index);
        if (!path.isEmpty()) paths << path;
    }
    paths.removeDuplicates();
    std::sort(paths.begin(), paths.end(), [](const auto &a, const auto &b) { return a.size() < b.size(); });
    QStringList result;
    for (const QString &path : paths) {
        bool covered = false;
        for (const QString &parent : result) {
#ifdef Q_OS_WIN
            constexpr auto cs = Qt::CaseInsensitive;
#else
            constexpr auto cs = Qt::CaseSensitive;
#endif
            if (path.startsWith(parent + '/', cs)) { covered = true; break; }
        }
        if (!covered) result << path;
    }
    return result;
}

void ExplorerTreeView::startDrag(Qt::DropActions)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return;
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : paths) urls << QUrl::fromLocalFile(path);
    mime->setUrls(urls);
    QDrag drag(this);
    drag.setMimeData(mime);
    // Only our drop handler performs the move. QAbstractItemView's default
    // startDrag can remove source rows after a move and must not be used here.
    drag.exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
}

QString ExplorerTreeView::destinationAt(const QPoint &position) const
{
    const QModelIndex index = indexAt(position);
    if (!index.isValid()) return workspacePath ? workspacePath() : QString();
    const QFileInfo info(pathForIndex ? pathForIndex(index) : QString());
    return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
}

void ExplorerTreeView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this && event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void ExplorerTreeView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->source() != this || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    // Keep Qt's auto-scroll behavior, then take ownership of drop acceptance.
    QTreeView::dragMoveEvent(event);
    event->setDropAction(event->modifiers().testFlag(Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
}

void ExplorerTreeView::dropEvent(QDropEvent *event)
{
    if (event->source() != this || !event->mimeData()->hasUrls() || !transferRequested) {
        event->ignore();
        return;
    }
    QStringList sources;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) { event->ignore(); return; }
        sources << url.toLocalFile();
    }
    const bool move = !event->modifiers().testFlag(Qt::ControlModifier);
    event->setDropAction(move ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
    transferRequested(sources, destinationAt(event->position().toPoint()), move);
}

void ExplorerTreeView::changeEvent(QEvent *event)
{
    QTreeView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        viewport()->update();
}

bool ExplorerTreeView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && handleShortcut
        && handleShortcut(static_cast<QKeyEvent *>(event), true)) {
        event->accept();
        return true;
    }
    return QTreeView::event(event);
}

void ExplorerTreeView::keyPressEvent(QKeyEvent *event)
{
    if (handleShortcut && handleShortcut(event, false)) {
        event->accept();
        return;
    }
    QTreeView::keyPressEvent(event);
}
