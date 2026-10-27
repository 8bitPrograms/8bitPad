// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QMap>
#include <QPoint>

class QAction;
class QActionGroup;
class QMenu;
class QSettings;

// Keeps language identities unchanged: a favorite and its grouped entry share
// the very same QAction, so editor selection stays in sync in both places.
class LanguageMenu : public QObject
{
    Q_OBJECT
public:
    struct Group { QString title; QStringList languages; };

    LanguageMenu(QMenu *menu, QActionGroup *actions, QSettings *settings,
                 QObject *parent = nullptr);
    void setLanguages(const QStringList &languages);
    static QList<Group> groups(const QStringList &languages);
    QStringList favorites() const;
    bool isFavorite(const QString &language) const;
    void setFavorite(const QString &language, bool favorite);
    QAction *actionForLanguage(const QString &language) const;

signals:
    void languageTriggered(const QString &language);
    void favoritesChanged(const QStringList &languages);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshFavorites();
    void openFavoriteMenu(QMenu *menu, QAction *action, const QPoint &position);
    QPointer<QMenu> menu;
    QPointer<QActionGroup> actionGroup;
    QPointer<QSettings> settings;
    QPointer<QMenu> favoritesMenu;
    QAction *favoritesSeparator = nullptr;
    QMap<QString, QAction *> languageActions;
    QList<QMenu *> groupMenus;
};
