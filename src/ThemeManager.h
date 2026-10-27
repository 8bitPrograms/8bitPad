// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class ApplicationSettings;
class EditorManager;
class LuaState;
class ScintillaNext;

struct EditorTheme
{
    QString id;
    QString name;
    bool isDark = true;
    QColor background;
    QColor foreground;
    QColor comment;
    QColor accent;
    QColor keyword;
    QColor string;
    QColor number;
    QColor function;
    QColor type;
    QColor error;
};

// Owns presentation only: switching themes never changes documents, selections,
// undo history, lexers, fonts, indentation, or the user's editing preferences.
class ThemeManager final : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(ApplicationSettings *settings, LuaState *lua,
                          EditorManager *editors, QObject *parent = nullptr);

    static const QList<EditorTheme> &catalog();
    QString currentThemeId() const { return mThemeId; }
    const EditorTheme &currentTheme() const;
    QString styleSheet() const;

public slots:
    // Accepts a catalog ID or its displayed name. Unknown values are rejected.
    bool setTheme(const QString &idOrName);
    void watchEditor(ScintillaNext *editor);
    void applyEditor(ScintillaNext *editor) const;

signals:
    void themeChanged(const QString &id);

private:
    void applyApplication() const;
    void applyLanguageStyles(ScintillaNext *editor, const QString &language,
                             int depth = 0) const;
    QColor syntaxColor(const QString &name) const;

    ApplicationSettings *mSettings;
    LuaState *mLua;
    QString mThemeId;
    QList<QPointer<ScintillaNext>> mEditors;
};
