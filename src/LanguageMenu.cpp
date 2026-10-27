// SPDX-License-Identifier: GPL-3.0-or-later
#include "LanguageMenu.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <utility>

namespace {
constexpr auto favoritesKey = "LanguageMenu/Favorites";
QString translated(const char *text) { return QCoreApplication::translate("LanguageMenu", text); }
void sortNames(QStringList &names)
{
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        const int compared = QString::compare(a, b, Qt::CaseInsensitive);
        return compared == 0 ? a < b : compared < 0;
    });
}
}

LanguageMenu::LanguageMenu(QMenu *menu, QActionGroup *actions, QSettings *settings, QObject *parent)
    : QObject(parent ? parent : menu), menu(menu), actionGroup(actions), settings(settings)
{
    connect(menu, &QMenu::aboutToShow, this, &LanguageMenu::refreshFavorites);
}

QList<LanguageMenu::Group> LanguageMenu::groups(const QStringList &languages)
{
    // This is intentionally an allowlist. Newly installed/unusual languages are
    // still reachable in Other rather than creating more top-level menus.
    const QList<Group> definitions = {
        {translated("C family"), {"C", "C++", "C#", "Objective-C"}},
        {translated("Scripting"), {"Python", "Ruby", "bash", "PowerShell", "Lua", "Batch"}},
        {translated("Web"), {"JavaScript", "HTML", "CSS", "SCSS", "PHP"}},
        {translated("JVM"), {"Java", "Groovy"}},
        {translated("Systems & Functional"), {"Rust", "Go", "Haskell"}},
        {translated("Documents & Data"), {"Text", "Markdown", "JSON", "YAML", "XML"}},
        {translated("Databases"), {"SQL", "SQL (Oracle)", "SQL (PostgreSQL)", "SQL (MSSQL)"}},
        {translated("DevOps & Build"), {"YAML (GitHub Actions)", "YAML (Azure Pipelines)",
                                      "YAML (Ansible)", "Jenkinsfile", "CMakeFile", "Makefile"}}
    };
    QSet<QString> remaining(languages.cbegin(), languages.cend());
    remaining.remove(QString());
    QList<Group> result;
    for (const auto &definition : definitions) {
        Group group {definition.title, {}};
        for (const auto &language : definition.languages) {
            if (remaining.remove(language))
                group.languages.append(language);
        }
        if (!group.languages.isEmpty()) {
            sortNames(group.languages);
            result.append(group);
        }
    }
    if (!remaining.isEmpty()) {
        QStringList other = remaining.values();
        sortNames(other);
        result.append({translated("Other"), other});
    }
    return result;
}

void LanguageMenu::setLanguages(const QStringList &languages)
{
    if (!menu || !actionGroup)
        return;
    const QString checkedLanguage = actionGroup->checkedAction()
        ? actionGroup->checkedAction()->data().toString() : QString();
    // The helper owns only its own menus/actions, never unrelated menu actions.
    for (QMenu *group : std::as_const(groupMenus))
        delete group;
    groupMenus.clear();
    delete favoritesMenu;
    favoritesMenu = nullptr;
    delete favoritesSeparator;
    favoritesSeparator = nullptr;
    qDeleteAll(languageActions);
    languageActions.clear();

    // Utility actions added by the caller (such as DevOps assistance) stay
    // after the language groups when an installed-language list is refreshed.
    QAction *before = menu->actions().isEmpty() ? nullptr : menu->actions().first();

    favoritesMenu = new QMenu(tr("Favorites"), menu);
    favoritesMenu->setObjectName(QStringLiteral("languageFavorites"));
    favoritesMenu->installEventFilter(this);
    menu->insertMenu(before, favoritesMenu);
    favoritesSeparator = menu->insertSeparator(before);

    for (const auto &group : groups(languages)) {
        QMenu *submenu = new QMenu(QString(group.title).replace(QLatin1Char('&'), QStringLiteral("&&")), menu);
        submenu->installEventFilter(this);
        groupMenus.append(submenu);
        menu->insertMenu(before, submenu);
        for (const auto &language : group.languages) {
            QAction *action = new QAction(language, this);
            action->setCheckable(true);
            action->setData(language);
            action->setStatusTip(tr("Right-click or press Menu to add or remove this language from Favorites."));
            actionGroup->addAction(action);
            languageActions.insert(language, action);
            submenu->addAction(action);
            connect(action, &QAction::triggered, this, [this, language] { emit languageTriggered(language); });
        }
    }
    if (QAction *selected = languageActions.value(checkedLanguage, nullptr))
        selected->setChecked(true);
    refreshFavorites();
}

QStringList LanguageMenu::favorites() const
{
    QStringList result = settings ? settings->value(QLatin1String(favoritesKey)).toStringList() : QStringList();
    result.removeAll(QString());
    result.removeDuplicates();
    return result;
}

bool LanguageMenu::isFavorite(const QString &language) const { return favorites().contains(language); }

void LanguageMenu::setFavorite(const QString &language, bool favorite)
{
    if (!settings || !languageActions.contains(language))
        return;
    QStringList names = favorites();
    const bool wasFavorite = names.contains(language);
    if (wasFavorite == favorite)
        return;
    if (favorite)
        names.append(language);
    else
        names.removeAll(language);
    settings->setValue(QLatin1String(favoritesKey), names);
    settings->sync();
    refreshFavorites();
    emit favoritesChanged(names);
}

QAction *LanguageMenu::actionForLanguage(const QString &language) const
{
    return languageActions.value(language, nullptr);
}

void LanguageMenu::refreshFavorites()
{
    if (!favoritesMenu)
        return;
    // removeAction, not clear: the shared language actions remain in the groups.
    for (QAction *action : favoritesMenu->actions())
        favoritesMenu->removeAction(action);
    QStringList names = favorites();
    sortNames(names);
    for (const auto &name : names) {
        if (QAction *action = languageActions.value(name, nullptr))
            favoritesMenu->addAction(action);
    }
    const bool available = !favoritesMenu->isEmpty();
    favoritesMenu->menuAction()->setVisible(available);
    if (favoritesSeparator)
        favoritesSeparator->setVisible(available);
}

void LanguageMenu::openFavoriteMenu(QMenu *source, QAction *action, const QPoint &position)
{
    if (!action || !languageActions.contains(action->data().toString()))
        return;
    const QString language = action->data().toString();
    const bool favorite = isFavorite(language);
    auto *popup = new QMenu(source);
    popup->setObjectName(QStringLiteral("languageFavoriteContextMenu"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    QAction *toggle = popup->addAction(favorite ? tr("Remove from Favorites") : tr("Add to Favorites"));
    connect(toggle, &QAction::triggered, this, [this, language, favorite] {
        setFavorite(language, !favorite);
    });
    popup->popup(position);
}

bool LanguageMenu::eventFilter(QObject *watched, QEvent *event)
{
    auto *source = qobject_cast<QMenu *>(watched);
    if (!source)
        return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::RightButton) {
            openFavoriteMenu(source, source->actionAt(mouse->position().toPoint()), mouse->globalPosition().toPoint());
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent *>(event)->button() == Qt::RightButton)
            return true;
    } else if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Menu || (key->key() == Qt::Key_F10 && key->modifiers().testFlag(Qt::ShiftModifier))) {
            QAction *action = source->activeAction();
            if (action) {
                openFavoriteMenu(source, action, source->mapToGlobal(source->actionGeometry(action).center()));
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
