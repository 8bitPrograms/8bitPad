// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShortcutDialog.h"
#include "MainWindow.h"
#include "ApplicationSettings.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

void showShortcutDialog(MainWindow *window, ApplicationSettings *settings)
{
    QDialog dialog(window);
    dialog.setWindowTitle(QObject::tr("Keyboard shortcuts"));
    dialog.resize(720, 540);
    auto *layout = new QVBoxLayout(&dialog);
    auto *filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(QObject::tr("Filter commands…"));
    auto *tree = new QTreeWidget(&dialog);
    tree->setHeaderLabels({QObject::tr("Command"), QObject::tr("Shortcut")});
    tree->setRootIsDecorated(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    auto *sequence = new QKeySequenceEdit(&dialog);
    auto *assign = new QPushButton(QObject::tr("Assign (empty removes shortcut)"), &dialog);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(filter);
    layout->addWidget(tree);
    layout->addWidget(sequence);
    layout->addWidget(assign);
    layout->addWidget(buttons);
    QHash<QTreeWidgetItem *, QAction *> actions;
    const auto allActions = window->findChildren<QAction *>();
    for (auto *action : allActions) {
        if (!action->objectName().startsWith("action") || action->isSeparator() || action->menu() || action->text().isEmpty()) continue;
        QString name = action->text(); name.remove('&');
        auto *item = new QTreeWidgetItem(tree, {name, action->shortcut().toString(QKeySequence::NativeText)});
        actions.insert(item, action);
    }
    tree->sortItems(0, Qt::AscendingOrder);
    QObject::connect(filter, &QLineEdit::textChanged, &dialog, [tree](const QString &query) {
        for (int i=0; i<tree->topLevelItemCount(); ++i) {
            auto *item=tree->topLevelItem(i);
            item->setHidden(!item->text(0).contains(query, Qt::CaseInsensitive));
        }
    });
    QObject::connect(tree, &QTreeWidget::currentItemChanged, &dialog, [&actions, sequence](QTreeWidgetItem *item) {
        if (auto *action=actions.value(item)) sequence->setKeySequence(action->shortcut());
    });
    QObject::connect(assign, &QPushButton::clicked, &dialog, [&] {
        auto *item=tree->currentItem();
        auto *action=actions.value(item);
        if (!action) return;
        const QKeySequence chosen=sequence->keySequence();
        if (!chosen.isEmpty()) {
            for (auto *other : allActions) {
                if (other == action || !other->isEnabled() || !other->isVisible()) continue;
                for (const auto &existing : other->shortcuts()) {
                    if (chosen.matches(existing) != QKeySequence::NoMatch || existing.matches(chosen) != QKeySequence::NoMatch) {
                        QMessageBox::warning(&dialog, QObject::tr("Shortcut already assigned"),
                            QObject::tr("This shortcut conflicts with %1. Clear that command first.").arg(other->text().remove('&')));
                        return;
                    }
                }
            }
        }
        action->setShortcuts(chosen.isEmpty() ? QList<QKeySequence>() : QList<QKeySequence>{chosen});
        settings->setValue("Shortcuts/" + action->objectName().mid(6),
            chosen.isEmpty() ? QStringList{} : QStringList{chosen.toString(QKeySequence::PortableText)});
        settings->sync();
        item->setText(1, chosen.toString(QKeySequence::NativeText));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (tree->topLevelItemCount()) tree->setCurrentItem(tree->topLevelItem(0));
    dialog.exec();
}
