/*
 * 8bitPad modal editing. Copyright 2026 8bitPad contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef VIMMODE_H
#define VIMMODE_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QByteArray>
#include <cstdint>

class ScintillaEdit;
class QKeyEvent;

// One controller belongs to one editor. All positions are Scintilla byte
// positions; positionBefore/After are used when stepping over UTF-8 characters.
class VimMode : public QObject
{
    Q_OBJECT
public:
    explicit VimMode(ScintillaEdit *editor, QObject *parent = nullptr);
    ~VimMode() override;

    bool isEnabled() const { return enabled; }
    QString modeName() const;

public slots:
    void setEnabled(bool value);

signals:
    void modeChanged(const QString &mode);
    void statusMessage(const QString &message);
    // Only implemented commands are emitted. Saving and close confirmation are
    // the application's responsibility, including saving successfully before wq.
    void exCommand(const QString &command);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    using Position = intptr_t;
    enum class Mode { Normal, Insert, Visual, VisualLine, Command };
    enum class RepeatKind { None, DeleteCharacters, DeleteLines, DeleteMotion, Paste, Insert };
    struct Repeat {
        RepeatKind kind = RepeatKind::None;
        QString motion;
        int count = 1;
        QByteArray text;
        bool linewise = false;
        bool before = false;
        bool explicitCount = false;
    };
    struct Motion {
        Position position = 0;
        bool inclusive = false;
        bool linewise = false;
        bool valid = false;
    };

    QPointer<ScintillaEdit> editor;
    bool enabled = false;
    Mode mode = Mode::Normal;
    Position savedCaretStyle = 1;
    Position visualAnchor = 0;
    Position visualCursor = 0;
    Position wantedColumn = -1;
    int count = 0;
    int operatorCount = 1;
    bool operatorCountExplicit = false;
    QChar pendingOperator;
    bool pendingG = false;
    QByteArray registerText;
    bool registerLinewise = false;
    QString prompt;
    QChar promptKind;
    QByteArray lastSearch;
    bool lastSearchBackward = false;
    Repeat lastChange;
    bool replaying = false;
    bool insertUndoOpen = false;
    bool recordingInsert = false;
    bool insertRepeatable = true;
    bool insertChanged = false;
    Position insertStart = 0;
    Position insertLength = 0;
    QString insertCommand;

    void setMode(Mode value);
    void resetPending();
    void showPending();
    bool handleKey(QKeyEvent *event);
    bool handlePrompt(QKeyEvent *event);
    bool claimsShortcut(QKeyEvent *event) const;
    Position cursor() const;
    Position normalPosition(Position position) const;
    Position firstNonBlank(Position line) const;
    QByteArray bytes(Position start, Position end) const;
    QByteArray eol() const;
    void replace(Position start, Position end, const QByteArray &text);
    void moveCursor(Position position, bool keepColumn = false);
    void refreshVisual();
    int characterClass(Position position) const;
    Position wordMotion(Position position, QChar direction, int repetitions) const;
    Motion calculateMotion(const QString &key, int repetitions, bool explicitCount);
    bool executeMotion(const QString &key);
    void applyOperator(QChar op, Position from, const Motion &motion, const QString &key, int repetitions, bool explicitCount = false);
    void operateLines(QChar op, Position firstLine, Position lastLine);
    void operateRange(QChar op, Position start, Position end, bool linewise);
    void deleteCharacters(int repetitions);
    void paste(bool before, int repetitions);
    void enterInsert(const QString &command);
    void leaveInsert();
    void performRepeat(int repetitions, bool explicitCount);
    void remember(const Repeat &change);
    void beginPrompt(QChar kind);
    void runPrompt();
    bool search(bool backward, int repetitions);
};

#endif
