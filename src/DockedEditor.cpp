/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
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


#include "DockedEditor.h"
#include "DockAreaTabBar.h"
#include "DockAreaWidget.h"
#include "DockWidgetTab.h"
#include "DockComponentsFactory.h"
#include "DockedEditorTitleBar.h"
#include "DockAreaTitleBar.h"

#include "ScintillaNext.h"
#include "UiIcons.h"

#include <QAbstractButton>
#include <QEvent>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>


class DockedEditorComponentsFactory : public ads::CDockComponentsFactory
{
public:
    ads::CDockAreaTitleBar* createDockAreaTitleBar(ads::CDockAreaWidget* DockArea) const {
        DockedEditorTitleBar *titleBar = new DockedEditorTitleBar(DockArea);

        // Disable the built in context menu for the title bar since it has options we don't want
        titleBar->setContextMenuPolicy(Qt::NoContextMenu);

        return titleBar;
    }
};


DockedEditor::DockedEditor(QWidget *parent) : QObject(parent)
{
    ads::CDockComponentsFactory::setFactory(new DockedEditorComponentsFactory());

    // ThemeManager owns the complete chrome. ADS otherwise reloads its own
    // stylesheet on light/dark transitions and overrides the application's tabs.
    ads::CDockManager::setConfigFlag(ads::CDockManager::DisableStylesheet, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AlwaysShowTabs, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewIsDynamic, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewShowsContentPixmap, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    // When tabs title/text elide disabled and lots of tabs opened, tabs menu button will not show
    // as it only shows when tab title elided. 
    // So disable dynamic tabs menu visibility.
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::MiddleMouseButtonClosesTab, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, false);

    dockManager = new ads::CDockManager(parent);
    dockManager->setStyleSheet("");
    dockManager->installEventFilter(this);

    connect(dockManager, &ads::CDockManager::focusedDockWidgetChanged, this, [=, this](ads::CDockWidget* old, ads::CDockWidget* now) {
        Q_UNUSED(old)

        // Focus can leave all docks while the logical active tab remains.
        if (!now) return;
        if (auto *editor = qobject_cast<ScintillaNext *>(now->widget())) {
            setCurrentEditor(editor);
            editor->grabFocus();
        }
    });

    connect(dockManager, &ads::CDockManager::dockAreaCreated, this, [=, this](ads::CDockAreaWidget* DockArea) {
        DockedEditorTitleBar *titleBar = qobject_cast<DockedEditorTitleBar *>(DockArea->titleBar());
        connect(titleBar, &DockedEditorTitleBar::doubleClicked, this, &DockedEditor::titleBarDoubleClicked);

        // Native/background windows may change tabs without receiving keyboard
        // focus. Keep document-dependent panels and recovery state in sync with
        // the selected page, independently of OS focus notifications.
        const auto activatePage = [this, DockArea] {
            if (auto *dock = DockArea->currentDockWidget())
                setCurrentEditor(qobject_cast<ScintillaNext *>(dock->widget()));
        };
        connect(DockArea, &ads::CDockAreaWidget::currentChanged, this, activatePage);
        connect(DockArea->titleBar()->tabBar(), &ads::CDockAreaTabBar::tabBarClicked, this, activatePage);

        connect(DockArea->titleBar()->tabBar(), &ads::CDockAreaTabBar::tabMoved, this, [=, this](int from, int to) {
            Q_UNUSED(from);
            Q_UNUSED(to);

            emit editorOrderChanged();
        });

        // In theory the order changes when a new dock area is created (e.g. editor is dragged and dropped),
        // but the dockAreaCreated() signal is triggered before it is actually added to the CDockManager,
        // so interrogating the dock manager during the signal doesn't help.
        //emit editorOrderChanged();
    });
}


ScintillaNext *DockedEditor::getCurrentEditor() const
{
    return currentEditor;
}

void DockedEditor::setCurrentEditor(ScintillaNext *editor)
{
    if (!editor || currentEditor == editor) return;
    currentEditor = editor;
    if (auto *dock = qobject_cast<ads::CDockWidget *>(editor->parentWidget()))
        latestDockArea = dock->dockAreaWidget();
    emit editorActivated(editor);
}

int DockedEditor::count() const
{
    int total = 0;

    for (int i = 0; i < dockManager->dockAreaCount(); ++i)
        total += dockManager->dockArea(i)->dockWidgetsCount();

    return total;
}

QVector<ScintillaNext *> DockedEditor::editors() const
{
    QVector<ScintillaNext *> editors;

    // For each area, for each widget, append it to our list
    for (const ads::CDockAreaWidget* areaWidget : dockManager->openedDockAreas()) {
        for (const ads::CDockWidget* dockWidget : areaWidget->dockWidgets()) {
            editors.append(qobject_cast<ScintillaNext *>(dockWidget->widget()));
        }
    }

    return editors;
}

void DockedEditor::switchToEditor(const ScintillaNext *editor)
{
    if (!editor) return;
    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());

    if (dockWidget == Q_NULLPTR) {
        qWarning() << "Expected editor's parent to be CDockWidget";
    }
    else {
        if (auto *area = dockWidget->dockAreaWidget())
            area->setCurrentDockWidget(dockWidget);
        dockWidget->raise();
        setCurrentEditor(qobject_cast<ScintillaNext *>(dockWidget->widget()));
    }
}

void DockedEditor::dockWidgetCloseRequested()
{
    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(sender());
    ScintillaNext *editor = qobject_cast<ScintillaNext *>(dockWidget->widget());

    emit editorCloseRequested(editor);
}

ads::CDockAreaWidget *DockedEditor::currentDockArea() const
{
    if (currentEditor) {
        if (auto *dock = qobject_cast<ads::CDockWidget *>(currentEditor->parentWidget()))
            if (dock->dockAreaWidget()) return dock->dockAreaWidget();
    }
    return latestDockArea.data();
}

void DockedEditor::addEditor(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    Q_ASSERT(editor != Q_NULLPTR);

    // Create the dock widget for the editor
    ads::CDockWidget *dockWidget = dockManager->createDockWidget(editor->getName());

    // Disable elide, elided file names not readable when lots of files opened
    dockWidget->tabWidget()->setElideMode(Qt::ElideNone);

    // We need a unique object name. Can't use the name or file path so use a uuid
    dockWidget->setObjectName(QUuid::createUuid().toString());

    dockWidget->setWidget(editor);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetDeleteOnClose, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::CustomCloseHandling, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetFloatable, false);

    dockWidget->tabWidget()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dockWidget->tabWidget(), &QWidget::customContextMenuRequested, this, [=, this](const QPoint &pos) {
        Q_UNUSED(pos)

        emit contextMenuRequestedForEditor(editor);
    });

    // Set the tooltip based on the buffer
    if (editor->isFile()) {
        dockWidget->tabWidget()->setToolTip(editor->getFilePath());
    }
    else {
        dockWidget->tabWidget()->setToolTip(editor->getName());
    }

    auto updateIcon = [editor, dockWidget] {
        auto *tab = dockWidget->tabWidget();
        tab->setIconSize(QSize(16, 16));
        tab->setIcon(documentTabIcon(QFileInfo(editor->isFile() ? editor->getFilePath() : editor->getName()),
                                    editor->canSaveToDisk(), editor->readOnly(), tab));
        if (auto *close = tab->findChild<QAbstractButton *>(QStringLiteral("tabCloseButton"))) {
            close->setIcon(uiIcon(UiIcon::Dismiss, tab));
            close->setIconSize(QSize(16, 16));
        }
    };
    updateIcon();
    dockWidget->tabWidget()->installEventFilter(this);
    connect(editor, &ScintillaNext::savePointChanged, dockWidget, [updateIcon](bool) { updateIcon(); });
    connect(editor, &ScintillaNext::renamed, dockWidget, updateIcon);
    connect(editor, &ScintillaNext::reloaded, dockWidget, updateIcon);

    connect(editor, &ScintillaNext::closed, dockWidget, &ads::CDockWidget::closeDockWidget);
    connect(editor, &ScintillaNext::closed, this, [=, this]() {
        if (currentEditor == editor) {
            currentEditor.clear();
            // The dock removal normally selects its neighbour synchronously.
            // If the last tab in an area closed, activate a surviving area.
            for (auto *candidate : editors()) {
                if (candidate && candidate != editor) {
                    switchToEditor(candidate);
                    break;
                }
            }
        }
        emit editorClosed(editor);
    });
    connect(editor, &ScintillaNext::renamed, this, [=, this]() { editorRenamed(editor); });

    connect(dockWidget, &ads::CDockWidget::closeRequested, this, &DockedEditor::dockWidgetCloseRequested);

    latestDockArea = dockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget, currentDockArea());

    emit editorAdded(editor);
    switchToEditor(editor);
}

void DockedEditor::editorRenamed(ScintillaNext *editor)
{
    Q_ASSERT(editor != Q_NULLPTR);

    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());

    dockWidget->setWindowTitle(editor->getName());

    if (editor->isFile()) {
        dockWidget->tabWidget()->setToolTip(editor->getFilePath());
    }
    else {
        dockWidget->tabWidget()->setToolTip(editor->getName());
    }
}

void DockedEditor::splitToRight(ScintillaNext *editor)
{
    Q_ASSERT(editor != Q_NULLPTR);

    ads::CDockWidget *newDockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());
    if (newDockWidget) {
        ads::CDockAreaWidget *currentArea = currentDockArea();
        if (currentArea) {
            dockManager->addDockWidget(ads::RightDockWidgetArea, newDockWidget, currentArea);
        }
    }
}

void DockedEditor::splitToBottom(ScintillaNext *editor)
{
    Q_ASSERT(editor != Q_NULLPTR);

    ads::CDockWidget *newDockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());
    if (newDockWidget) {
        ads::CDockAreaWidget *currentArea = currentDockArea();
        if (currentArea) {
            dockManager->addDockWidget(ads::BottomDockWidgetArea, newDockWidget, currentArea);
        }
    }
}

bool DockedEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::StyleChange || event->type() == QEvent::DevicePixelRatioChange) {
        // ADS stores tab icons in QLabel pixmaps. Re-render once after a theme
        // or display-scale change instead of keeping the previous palette.
        if (!iconRefreshQueued) {
            iconRefreshQueued = true;
            QTimer::singleShot(0, this, [this] { iconRefreshQueued = false; refreshTabIcons(); });
        }
    }
    return QObject::eventFilter(watched, event);
}

void DockedEditor::refreshTabIcons()
{
    for (ScintillaNext *editor : editors()) {
        if (!editor) continue;
        auto *dock = qobject_cast<ads::CDockWidget *>(editor->parentWidget());
        if (!dock) continue;
        auto *tab = dock->tabWidget();
        tab->setIcon(documentTabIcon(QFileInfo(editor->isFile() ? editor->getFilePath() : editor->getName()),
                                    editor->canSaveToDisk(), editor->readOnly(), tab));
        if (auto *close = tab->findChild<QAbstractButton *>(QStringLiteral("tabCloseButton")))
            close->setIcon(uiIcon(UiIcon::Dismiss, tab));
    }
}
