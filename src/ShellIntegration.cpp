// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShellIntegration.h"
#include "FileAssociationsDialog.h"

#include <QAction>
#include <QMenu>
#include <QWidget>

void showShellIntegrationDialog(QWidget *parent)
{
    FileAssociationsDialog dialog(parent);
    dialog.exec();
}

void installShellIntegration(QWidget *window)
{
    auto *action = new QAction(QObject::tr("File Associations…"), window);
    action->setObjectName(QStringLiteral("actionFileAssociations"));
    if (auto *file = window->findChild<QMenu *>(QStringLiteral("menuFile"))) {
        if (auto *reload = window->findChild<QAction *>(QStringLiteral("actionReload"))) file->insertAction(reload, action);
        else file->addAction(action);
    }
    QObject::connect(action, &QAction::triggered, window, [window] { showShellIntegrationDialog(window); });
}
