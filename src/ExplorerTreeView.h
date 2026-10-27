// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTreeView>
#include <functional>

class QKeyEvent;

class ExplorerTreeView : public QTreeView
{
public:
    explicit ExplorerTreeView(QWidget *parent = nullptr);
    QStringList selectedPaths() const;
    std::function<QString(const QModelIndex &)> pathForIndex;
    std::function<QString()> workspacePath;
    std::function<void(const QStringList &, const QString &, bool)> transferRequested;
    std::function<bool(QKeyEvent *, bool)> handleShortcut;

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QString destinationAt(const QPoint &position) const;
};
