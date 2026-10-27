/*
 * This file is part of Notepad Next.
 * Copyright 2026 Justin Dailey
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


#include <QApplication>
#include <QToolButton>
#include <QStyle>
#include <QMenu>
#include <QEvent>
#include <QPainter>
#include <QSvgRenderer>

#include <map>

#include "TabsQuickActionsBar.h"

namespace
{
    constexpr QLatin1StringView IconPlusPath(":/icons/plus.svg");
    constexpr QLatin1StringView IconListPath(":/icons/list_with_icons.svg");
    constexpr QLatin1StringView IconCrossPath(":/icons/cross.svg");

    QIcon tintedIcon(const QString &path, const QPalette &palette)
    {
        QSvgRenderer renderer(path);
        QIcon icon;
        // Multiple raster sizes preserve crisp edges on Windows/Linux HiDPI
        // displays. SVG currentColor otherwise defaults to black in QIcon.
        for (const int size : {16, 20, 24, 32, 48, 64}) {
            for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
                QPixmap pixmap(size, size);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                renderer.render(&painter);
                painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
                painter.fillRect(pixmap.rect(), palette.color(group, QPalette::ButtonText));
                painter.end();
                icon.addPixmap(pixmap, mode);
            }
        }
        return icon;
    }
}

TabsQuickActionsBar::TabsQuickActionsBar(const Buttons &visibileButtons, QWidget *parent)
    : QToolBar(parent)
{
    createNewTabAction = addAction(QIcon(), "");
    createNewTabAction->setToolTip(tr("Create a new file"));

    showTabsMenuAction = addAction(QIcon(), "");
    showTabsMenuAction->setToolTip(tr("Show opened files list"));

    const auto tabsMenu = new QMenu(this);
    showTabsMenuAction->setMenu(tabsMenu);

    closeCurrentTabAction = addAction(QIcon(), "");
    closeCurrentTabAction->setToolTip(tr("Close the current file"));

    const auto iconSize = qApp->style()->pixelMetric(QStyle::PM_SmallIconSize);
    setIconSize({ iconSize, iconSize });
    setStyleSheet(
        "QToolBar { padding: 0px; margin: 0px; }"
        "QToolButton::menu-indicator { image: none; }"
    );

    // Trick, cause addWidget will lose some style things
    const auto toolButton = qobject_cast<QToolButton*>(widgetForAction(showTabsMenuAction));
    if (toolButton) toolButton->setPopupMode(QToolButton::InstantPopup);

    connect(createNewTabAction, &QAction::triggered, this, &TabsQuickActionsBar::createNewTabClicked);
    connect(tabsMenu, &QMenu::aboutToShow, this, [this, tabsMenu]() { emit tabsMenuAboutToShow(tabsMenu); });
    connect(closeCurrentTabAction, &QAction::triggered, this, &TabsQuickActionsBar::closeCurrentTabClicked);

    setVisibileButtons(visibileButtons);
    refreshIcons();
}

void TabsQuickActionsBar::changeEvent(QEvent *event)
{
    QToolBar::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange)
        refreshIcons();
}

void TabsQuickActionsBar::refreshIcons()
{
    // A palette event may arrive before construction has created all actions.
    if (createNewTabAction) createNewTabAction->setIcon(tintedIcon(IconPlusPath, palette()));
    if (showTabsMenuAction) showTabsMenuAction->setIcon(tintedIcon(IconListPath, palette()));
    if (closeCurrentTabAction) closeCurrentTabAction->setIcon(tintedIcon(IconCrossPath, palette()));
}

void TabsQuickActionsBar::setVisibileButtons(const Buttons &buttons)
{
    if (visibileButtons == buttons)
        return;

    visibileButtons = buttons;

    const std::map<QAction*, Button> mapping {
        { createNewTabAction,    CreateNewTab    },
        { showTabsMenuAction,    ShowTabsMenu    },
        { closeCurrentTabAction, CloseCurrentTab }
    };

    for (const auto &pair : mapping)
        pair.first->setVisible(buttons.testFlag(pair.second));

    emit visibileButtonsChanged(buttons);
}

void TabsQuickActionsBar::setVisibileButton(Button button, bool on)
{
    const auto &currentOptions = visibileButtons;
    setVisibileButtons(
        on ? (currentOptions |  button)
           : (currentOptions & ~button)
    );
}
