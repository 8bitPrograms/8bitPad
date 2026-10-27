/*
 * 8bitPad modal editing. Copyright 2026 8bitPad contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "VimMode.h"
#include "ScintillaEdit.h"

#include <QEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <algorithm>
#include <limits>

namespace {
constexpr int MaxCount = 100000;
constexpr qint64 MaxPasteBytes = 64 * 1024 * 1024;

// Single normal-mode changes and a whole insert session are single undo units.
class UndoGroup {
public:
    explicit UndoGroup(ScintillaEdit *value) : editor(value) { editor->beginUndoAction(); }
    ~UndoGroup() { editor->endUndoAction(); }
private:
    ScintillaEdit *editor;
};
}

VimMode::VimMode(ScintillaEdit *value, QObject *parent)
    : QObject(parent ? parent : value), editor(value)
{
    Q_ASSERT(value);
    value->installEventFilter(this);
    value->viewport()->installEventFilter(this);

    // Record only a contiguous insert change. Navigation followed by editing
    // outside that change invalidates dot-repeat instead of guessing at edits.
    connect(value, &ScintillaEdit::modified, this,
        [this](Scintilla::ModificationFlags type, Scintilla::Position position,
               Scintilla::Position length, Scintilla::Position, const QByteArray &,
               Scintilla::Position, Scintilla::FoldLevel, Scintilla::FoldLevel) {
        if (!recordingInsert) return;
        const int flags = static_cast<int>(type);
        if (!(flags & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) return;
        insertChanged = true;
        if (flags & (SC_PERFORMED_UNDO | SC_PERFORMED_REDO)) insertRepeatable = false;
        if (flags & SC_MOD_INSERTTEXT) {
            if (position < insertStart || position > insertStart + insertLength) {
                insertRepeatable = false;
            } else {
                insertLength += length;
            }
        } else if (flags & SC_MOD_DELETETEXT) {
            if (position < insertStart || position + length > insertStart + insertLength) {
                insertRepeatable = false;
            } else {
                insertLength -= length;
            }
        }
    });
}

VimMode::~VimMode()
{
    // QWidget destroys child QObjects before its QObject base clears QPointer.
    // At that point Scintilla's internal child may already be gone. End the
    // group only when this controller is removed from a still-live editor.
    if (editor && qobject_cast<ScintillaEdit *>(editor.data()) && insertUndoOpen)
        editor->endUndoAction();
}

QString VimMode::modeName() const
{
    if (!enabled) return tr("EDIT");
    switch (mode) {
    case Mode::Normal: return tr("NORMAL");
    case Mode::Insert: return tr("INSERT");
    case Mode::Visual: return tr("VISUAL");
    case Mode::VisualLine: return tr("VISUAL LINE");
    case Mode::Command: return tr("COMMAND");
    }
    return {};
}

void VimMode::setEnabled(bool value)
{
    if (!editor || enabled == value) return;
    if (mode == Mode::Insert) leaveInsert();
    if (value) savedCaretStyle = editor->caretStyle();
    enabled = value;
    mode = Mode::Normal;
    resetPending();
    editor->setCaretStyle(value ? CARETSTYLE_BLOCK : savedCaretStyle);
    if (value) moveCursor(editor->currentPos());
    emit modeChanged(modeName());
    emit statusMessage({});
}

void VimMode::setMode(Mode value)
{
    mode = value;
    editor->setCaretStyle(mode == Mode::Insert ? savedCaretStyle : CARETSTYLE_BLOCK);
    emit modeChanged(modeName());
}

void VimMode::resetPending()
{
    count = 0;
    operatorCount = 1;
    operatorCountExplicit = false;
    pendingOperator = QChar();
    pendingG = false;
}

void VimMode::showPending()
{
    QString text;
    if (!pendingOperator.isNull()) {
        if (operatorCount > 1) text += QString::number(operatorCount);
        text += pendingOperator;
    }
    if (count) text += QString::number(count);
    if (pendingG) text += QLatin1Char('g');
    emit statusMessage(text);
}

bool VimMode::claimsShortcut(QKeyEvent *event) const
{
    const auto modifiers = event->modifiers();
    if (modifiers & (Qt::AltModifier | Qt::MetaModifier)) return false;
    if (event->key() == Qt::Key_Escape) return true;
    if (modifiers & Qt::ControlModifier) {
        return event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_C ||
            (mode != Mode::Insert && event->key() == Qt::Key_R);
    }
    if (mode == Mode::Insert) return false;
    return !event->text().isEmpty() || event->key() == Qt::Key_Left ||
        event->key() == Qt::Key_Right || event->key() == Qt::Key_Up ||
        event->key() == Qt::Key_Down || event->key() == Qt::Key_Home ||
        event->key() == Qt::Key_End || event->key() == Qt::Key_Backspace ||
        event->key() == Qt::Key_Delete || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter;
}

bool VimMode::eventFilter(QObject *object, QEvent *event)
{
    if (!enabled || !editor) return QObject::eventFilter(object, event);
    if (event->type() == QEvent::ShortcutOverride && claimsShortcut(static_cast<QKeyEvent *>(event))) {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) return handleKey(static_cast<QKeyEvent *>(event));
    if (event->type() == QEvent::InputMethod && mode != Mode::Insert) {
        // IME commits are text only in command/search prompts, never normal mode.
        if (mode == Mode::Command) {
            prompt += static_cast<QInputMethodEvent *>(event)->commitString();
            emit statusMessage(QString(promptKind) + prompt);
        }
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress && mode != Mode::Insert) {
        resetPending();
        if (mode != Mode::Normal) setMode(Mode::Normal);
        emit statusMessage({});
    }
    return QObject::eventFilter(object, event);
}

VimMode::Position VimMode::cursor() const
{
    return (mode == Mode::Visual || mode == Mode::VisualLine) ? visualCursor : normalPosition(editor->currentPos());
}

VimMode::Position VimMode::normalPosition(Position position) const
{
    position = std::clamp<Position>(position, 0, editor->length());
    const Position line = editor->lineFromPosition(position);
    const Position start = editor->positionFromLine(line);
    const Position end = editor->lineEndPosition(line);
    if (position >= end && end > start) return editor->positionBefore(end);
    return std::min(position, end);
}

VimMode::Position VimMode::firstNonBlank(Position line) const
{
    Position position = editor->positionFromLine(line);
    const Position end = editor->lineEndPosition(line);
    while (position < end && (editor->charAt(position) == ' ' || editor->charAt(position) == '\t'))
        position = editor->positionAfter(position);
    return position;
}

QByteArray VimMode::bytes(Position start, Position end) const
{
    if (end <= start) return {};
    // Full-width range messages avoid truncating byte positions in large files.
    QByteArray result(end - start + 1, '\0');
    Sci_TextRangeFull range {{start, end}, result.data()};
    editor->send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&range));
    result.resize(end - start);
    return result;
}

QByteArray VimMode::eol() const
{
    switch (editor->eOLMode()) {
    case SC_EOL_CRLF: return "\r\n";
    case SC_EOL_CR: return "\r";
    default: return "\n";
    }
}

void VimMode::replace(Position start, Position end, const QByteArray &text)
{
    editor->setTargetRange(start, end);
    editor->replaceTarget(text.size(), text.constData());
}

void VimMode::moveCursor(Position position, bool keepColumn)
{
    position = normalPosition(position);
    if (!keepColumn) wantedColumn = -1;
    if (mode == Mode::Visual || mode == Mode::VisualLine) {
        visualCursor = position;
        refreshVisual();
    } else {
        editor->setSel(position, position);
    }
    editor->scrollCaret();
}

void VimMode::refreshVisual()
{
    Position start = std::min(visualAnchor, visualCursor);
    Position end = std::max(visualAnchor, visualCursor);
    if (mode == Mode::VisualLine) {
        start = editor->positionFromLine(editor->lineFromPosition(start));
        const Position lastLine = editor->lineFromPosition(end);
        end = lastLine + 1 < editor->lineCount() ? editor->positionFromLine(lastLine + 1) : editor->length();
    } else if (end < editor->length()) {
        // An empty line's visual character is its complete line ending.
        // Scintilla positionAfter intentionally treats CR and LF separately.
        if (editor->charAt(end) == '\r' && editor->charAt(end + 1) == '\n') end += 2;
        else end = editor->positionAfter(end);
    }
    if (visualCursor < visualAnchor) editor->setSel(end, start);
    else editor->setSel(start, end);
}

int VimMode::characterClass(Position position) const
{
    if (position >= editor->length()) return 0;
    const QString character = QString::fromUtf8(bytes(position, editor->positionAfter(position)));
    if (character.isEmpty() || character.at(0).isSpace()) return 0;
    const auto codepoint = character.toUcs4().at(0);
    return QChar::isLetterOrNumber(codepoint) || codepoint == '_' ? 1 : 2;
}

VimMode::Position VimMode::wordMotion(Position position, QChar direction, int repetitions) const
{
    const Position length = editor->length();
    for (int n = 0; n < repetitions; ++n) {
        const Position original = position;
        if (direction == QLatin1Char('b')) {
            if (position == 0) break;
            position = editor->positionBefore(position);
            while (position > 0 && characterClass(position) == 0) position = editor->positionBefore(position);
            const int category = characterClass(position);
            while (position > 0 && characterClass(editor->positionBefore(position)) == category)
                position = editor->positionBefore(position);
        } else if (direction == QLatin1Char('w')) {
            const int category = characterClass(position);
            while (position < length && characterClass(position) == category)
                position = editor->positionAfter(position);
            while (position < length && characterClass(position) == 0)
                position = editor->positionAfter(position);
        } else {
            if (position < length) position = editor->positionAfter(position);
            while (position < length && characterClass(position) == 0)
                position = editor->positionAfter(position);
            const int category = characterClass(position);
            while (position < length) {
                const Position next = editor->positionAfter(position);
                if (next >= length || characterClass(next) != category) break;
                position = next;
            }
        }
        if (position == original) break;
    }
    return position;
}

VimMode::Motion VimMode::calculateMotion(const QString &key, int repetitions, bool explicitCount)
{
    Motion result;
    result.valid = true;
    result.position = cursor();
    const Position line = editor->lineFromPosition(result.position);
    const Position lineStart = editor->positionFromLine(line);
    const Position lineEnd = editor->lineEndPosition(line);
    if (key == QLatin1String("h") || key == QLatin1String("l")) {
        for (int n = 0; n < repetitions; ++n) {
            const Position next = key == QLatin1String("h") ? editor->positionBefore(result.position) : editor->positionAfter(result.position);
            if (next < lineStart || next > lineEnd || next == result.position) break;
            result.position = next;
        }
    } else if (key == QLatin1String("j") || key == QLatin1String("k")) {
        if (wantedColumn < 0) wantedColumn = editor->column(result.position);
        const Position targetLine = std::clamp<Position>(line + (key == QLatin1String("j") ? repetitions : -repetitions), 0, editor->lineCount() - 1);
        result.position = editor->findColumn(targetLine, wantedColumn);
        result.linewise = true;
    } else if (key == QLatin1String("w") || key == QLatin1String("b") || key == QLatin1String("e")) {
        result.position = wordMotion(result.position, key.at(0), repetitions);
        result.inclusive = key == QLatin1String("e");
    } else if (key == QLatin1String("0")) {
        result.position = lineStart;
    } else if (key == QLatin1String("^")) {
        result.position = firstNonBlank(line);
    } else if (key == QLatin1String("$")) {
        result.position = editor->lineEndPosition(std::min<Position>(line + repetitions - 1, editor->lineCount() - 1));
        if (result.position > editor->positionFromLine(editor->lineFromPosition(result.position)))
            result.position = editor->positionBefore(result.position);
        result.inclusive = true;
    } else if (key == QLatin1String("gg") || key == QLatin1String("G")) {
        Position targetLine = key == QLatin1String("gg") ? 0 : editor->lineCount() - 1;
        if (explicitCount) targetLine = std::min<Position>(repetitions - 1, editor->lineCount() - 1);
        result.position = firstNonBlank(targetLine);
        result.linewise = true;
    } else {
        result.valid = false;
    }
    return result;
}

bool VimMode::executeMotion(const QString &key)
{
    const bool explicitCount = count > 0 || operatorCountExplicit;
    const int repetitions = std::min<qint64>(MaxCount, qint64(std::max(1, count)) * operatorCount);
    const Motion motion = calculateMotion(key, repetitions, explicitCount);
    if (!motion.valid) return false;
    const Position from = cursor();
    if (!pendingOperator.isNull()) {
        applyOperator(pendingOperator, from, motion, key, repetitions, explicitCount);
    } else {
        moveCursor(motion.position, key == QLatin1String("j") || key == QLatin1String("k"));
    }
    resetPending();
    return true;
}

void VimMode::applyOperator(QChar op, Position from, const Motion &motion, const QString &key, int repetitions, bool explicitCount)
{
    if (motion.linewise) {
        operateLines(op, editor->lineFromPosition(std::min(from, motion.position)), editor->lineFromPosition(std::max(from, motion.position)));
    } else {
        Position start = std::min(from, motion.position);
        Position end = std::max(from, motion.position);
        if (motion.inclusive && end < editor->length()) {
            const Position endOfLine = editor->lineEndPosition(editor->lineFromPosition(end));
            if (end < endOfLine) end = editor->positionAfter(end);
        }
        // Vim's dw on the final word of a line retains its line ending.
        if (key == QLatin1String("w") && editor->lineFromPosition(end) > editor->lineFromPosition(from) &&
            repetitions == 1 && characterClass(from) != 0) {
            end = editor->lineEndPosition(editor->lineFromPosition(from));
        }
        operateRange(op, start, end, false);
    }
    if (op == QLatin1Char('d')) remember({RepeatKind::DeleteMotion, key, repetitions, {}, false, false, explicitCount});
}

void VimMode::operateLines(QChar op, Position firstLine, Position lastLine)
{
    firstLine = std::clamp<Position>(firstLine, 0, editor->lineCount() - 1);
    lastLine = std::clamp<Position>(lastLine, firstLine, editor->lineCount() - 1);
    const Position start = editor->positionFromLine(firstLine);
    const Position end = lastLine + 1 < editor->lineCount() ? editor->positionFromLine(lastLine + 1) : editor->length();
    operateRange(op, start, end, true);
}

void VimMode::operateRange(QChar op, Position start, Position end, bool linewise)
{
    if (op == QLatin1Char('d') && editor->readOnly()) {
        emit statusMessage(tr("Document is read-only"));
        return;
    }
    if (end <= start && !linewise) return;
    registerText = bytes(start, end);
    registerLinewise = linewise;
    if (linewise && !registerText.endsWith('\n') && !registerText.endsWith('\r')) registerText += eol();
    if (op == QLatin1Char('d')) {
        UndoGroup group(editor);
        const Position firstLine = editor->lineFromPosition(start);
        // Deleting the final line also removes its preceding line ending. The
        // register contains only the requested lines, not that preceding EOL.
        const bool finalLineHasText = end > 0 && editor->charAt(end - 1) != '\n' && editor->charAt(end - 1) != '\r';
        if (linewise && end == editor->length() && start > 0 && (finalLineHasText || start == end))
            start = editor->lineEndPosition(firstLine - 1);
        replace(start, end, {});
        if (linewise) start = firstNonBlank(std::min<Position>(firstLine, editor->lineCount() - 1));
    }
    setMode(Mode::Normal);
    moveCursor(start);
}

void VimMode::deleteCharacters(int repetitions)
{
    const Position start = cursor();
    const Position endOfLine = editor->lineEndPosition(editor->lineFromPosition(start));
    Position end = start;
    while (repetitions-- > 0 && end < endOfLine) end = editor->positionAfter(end);
    operateRange(QLatin1Char('d'), start, end, false);
}

void VimMode::paste(bool before, int repetitions)
{
    if (registerText.isEmpty()) {
        emit statusMessage(tr("The unnamed register is empty"));
        return;
    }
    if (editor->readOnly()) {
        emit statusMessage(tr("Document is read-only"));
        return;
    }
    if (qint64(registerText.size()) * repetitions > MaxPasteBytes) {
        emit statusMessage(tr("Vim paste is limited to 64 MiB per command"));
        return;
    }
    UndoGroup group(editor);
    const QByteArray content = registerText.repeated(repetitions);
    QByteArray insertion = content;
    Position position = cursor();
    Position destination = position;
    if (registerLinewise) {
        const Position line = editor->lineFromPosition(position);
        if (before) {
            position = editor->positionFromLine(line);
            destination = position;
        } else if (line + 1 < editor->lineCount()) {
            position = editor->positionFromLine(line + 1);
            destination = position;
        } else if (editor->length() == 0) {
            position = destination = 0;
            if (insertion.endsWith(eol())) insertion.chop(eol().size());
        } else {
            position = editor->length();
            insertion.prepend(eol());
            if (insertion.endsWith(eol())) insertion.chop(eol().size());
            destination = position + eol().size();
        }
    } else {
        const Position end = editor->lineEndPosition(editor->lineFromPosition(position));
        if (!before && position < end) position = editor->positionAfter(position);
        destination = position + insertion.size();
    }
    replace(position, position, insertion);
    if (!registerLinewise && destination > position) destination = editor->positionBefore(destination);
    moveCursor(destination);
    remember({RepeatKind::Paste, {}, repetitions, registerText, registerLinewise, before});
}

void VimMode::enterInsert(const QString &command)
{
    if (editor->readOnly()) {
        emit statusMessage(tr("Document is read-only"));
        return;
    }
    Position position = cursor();
    const Position line = editor->lineFromPosition(position);
    const Position end = editor->lineEndPosition(line);
    editor->beginUndoAction();
    insertUndoOpen = true;
    insertCommand = command;
    insertChanged = command == QLatin1String("o") || command == QLatin1String("O");
    insertRepeatable = true;
    if (command == QLatin1String("a") && position < end) position = editor->positionAfter(position);
    else if (command == QLatin1String("I")) position = firstNonBlank(line);
    else if (command == QLatin1String("A")) position = end;
    else if (command == QLatin1String("o")) {
        position = end;
        replace(position, position, eol());
        position += eol().size();
    } else if (command == QLatin1String("O")) {
        position = editor->positionFromLine(line);
        replace(position, position, eol());
    }
    editor->setSel(position, position);
    insertStart = position;
    insertLength = 0;
    recordingInsert = true;
    setMode(Mode::Insert);
    resetPending();
    emit statusMessage({});
    editor->scrollCaret();
}

void VimMode::leaveInsert()
{
    recordingInsert = false;
    if (insertChanged) {
        if (insertRepeatable) remember({RepeatKind::Insert, insertCommand, 1, bytes(insertStart, insertStart + insertLength)});
        else remember({});
    }
    if (insertUndoOpen) {
        editor->endUndoAction();
        insertUndoOpen = false;
    }
    Position position = editor->currentPos();
    if (position > editor->positionFromLine(editor->lineFromPosition(position))) position = editor->positionBefore(position);
    setMode(Mode::Normal);
    moveCursor(position);
}

void VimMode::remember(const Repeat &change)
{
    if (!replaying && !editor->readOnly()) lastChange = change;
}

void VimMode::performRepeat(int repetitions, bool explicitCount)
{
    const Repeat change = lastChange;
    if (change.kind == RepeatKind::None) {
        emit statusMessage(tr("No repeatable change; use an insert or delete command first"));
        return;
    }
    if (editor->readOnly()) {
        emit statusMessage(tr("Document is read-only"));
        return;
    }
    const int amount = explicitCount ? repetitions : change.count;
    replaying = true;
    {
        UndoGroup group(editor);
        switch (change.kind) {
        case RepeatKind::DeleteCharacters: deleteCharacters(amount); break;
        case RepeatKind::DeleteLines: {
            const Position line = editor->lineFromPosition(cursor());
            operateLines(QLatin1Char('d'), line, line + amount - 1);
            break;
        }
        case RepeatKind::DeleteMotion: {
            const Motion motion = calculateMotion(change.motion, amount, explicitCount || change.explicitCount);
            applyOperator(QLatin1Char('d'), cursor(), motion, change.motion, amount, explicitCount || change.explicitCount);
            break;
        }
        case RepeatKind::Paste:
            registerText = change.text;
            registerLinewise = change.linewise;
            paste(change.before, amount);
            break;
        case RepeatKind::Insert:
            if (qint64(change.text.size()) * amount > MaxPasteBytes) {
                emit statusMessage(tr("Vim repeat is limited to 64 MiB per command"));
                break;
            }
            for (int i = 0; i < amount; ++i) {
                enterInsert(change.motion);
                const Position position = editor->currentPos();
                replace(position, position, change.text);
                editor->setSel(position + change.text.size(), position + change.text.size());
                leaveInsert();
            }
            break;
        case RepeatKind::None: break;
        }
    }
    replaying = false;
}

void VimMode::beginPrompt(QChar kind)
{
    promptKind = kind;
    prompt.clear();
    resetPending();
    setMode(Mode::Command);
    emit statusMessage(QString(kind));
}

bool VimMode::handlePrompt(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        runPrompt();
        return true;
    }
    if (event->key() == Qt::Key_Backspace) {
        if (!prompt.isEmpty()) {
            // Remove a Unicode scalar, preserving supplementary-plane text.
            const bool pair = prompt.size() > 1 && prompt.back().isLowSurrogate() && prompt.at(prompt.size() - 2).isHighSurrogate();
            prompt.chop(pair ? 2 : 1);
        }
        emit statusMessage(QString(promptKind) + prompt);
        return true;
    }
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        !event->text().isEmpty() && event->text().at(0).isPrint()) {
        prompt += event->text();
        emit statusMessage(QString(promptKind) + prompt);
    }
    return true;
}

void VimMode::runPrompt()
{
    const QChar kind = promptKind;
    const QString command = prompt;
    setMode(Mode::Normal);
    moveCursor(editor->currentPos());
    emit statusMessage({});
    if (kind == QLatin1Char(':')) {
        const QString value = command.trimmed();
        if (value == QLatin1String("w") || value == QLatin1String("q") || value == QLatin1String("wq") || value == QLatin1String("q!")) {
            emit exCommand(value);
        } else if (!value.isEmpty()) {
            emit statusMessage(tr("Unsupported Vim command: :%1").arg(value));
        }
    } else {
        if (!command.isEmpty()) lastSearch = command.toUtf8();
        lastSearchBackward = kind == QLatin1Char('?');
        search(lastSearchBackward, 1);
    }
}

bool VimMode::search(bool backward, int repetitions)
{
    if (lastSearch.isEmpty()) {
        emit statusMessage(tr("No previous search"));
        return false;
    }
    Position found = cursor();
    const Position length = editor->length();
    const auto savedFlags = editor->searchFlags();
    const auto savedStart = editor->targetStart();
    const auto savedEnd = editor->targetEnd();
    editor->setSearchFlags(SCFIND_MATCHCASE);
    bool wrapped = false;
    for (int n = 0; n < repetitions; ++n) {
        const Position start = backward ? found : (found < length ? editor->positionAfter(found) : length);
        editor->setTargetRange(start, backward ? 0 : length);
        Position match = editor->searchInTarget(lastSearch.size(), lastSearch.constData());
        if (match < 0) {
            // Search the whole document after wrapping. Bounding this range at
            // the old caret would exclude a sole match that crosses that bound.
            editor->setTargetRange(backward ? length : 0, backward ? 0 : length);
            match = editor->searchInTarget(lastSearch.size(), lastSearch.constData());
            wrapped = true;
        }
        if (match < 0) {
            editor->setSearchFlags(savedFlags);
            editor->setTargetRange(savedStart, savedEnd);
            emit statusMessage(tr("Pattern not found: %1").arg(QString::fromUtf8(lastSearch)));
            return false;
        }
        found = match;
    }
    editor->setSearchFlags(savedFlags);
    editor->setTargetRange(savedStart, savedEnd);
    moveCursor(found);
    emit statusMessage(wrapped ? tr("Search wrapped") : QString());
    return true;
}

bool VimMode::handleKey(QKeyEvent *event)
{
    const auto modifiers = event->modifiers();
    const bool control = modifiers & Qt::ControlModifier;
    if (modifiers & (Qt::AltModifier | Qt::MetaModifier)) return false;
    if (event->key() == Qt::Key_Escape || (control && (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_C))) {
        const Position position = cursor();
        if (mode == Mode::Insert) leaveInsert();
        else {
            setMode(Mode::Normal);
            moveCursor(position);
        }
        resetPending();
        emit statusMessage({});
        return true;
    }
    if (mode == Mode::Insert) return false;
    if (mode == Mode::Command) return handlePrompt(event);
    if (control) {
        if (event->key() == Qt::Key_R) {
            resetPending();
            setMode(Mode::Normal);
            editor->redo();
            moveCursor(editor->currentPos());
            return true;
        }
        return false; // Keep application shortcuts such as Ctrl+S and Ctrl+F.
    }

    QString key = event->text();
    switch (event->key()) {
    case Qt::Key_Left: key = QStringLiteral("h"); break;
    case Qt::Key_Right: key = QStringLiteral("l"); break;
    case Qt::Key_Up: key = QStringLiteral("k"); break;
    case Qt::Key_Down: key = QStringLiteral("j"); break;
    case Qt::Key_Home: key = QStringLiteral("0"); break;
    case Qt::Key_End: key = QStringLiteral("$"); break;
    case Qt::Key_Backspace: key = QStringLiteral("h"); break;
    case Qt::Key_Delete: key = QStringLiteral("x"); break;
    default: break;
    }
    if (key.isEmpty()) return false;
    if (key.size() != 1) return true;
    const QChar character = key.at(0);
    emit statusMessage({});

    if (character >= QLatin1Char('0') && character <= QLatin1Char('9') && (character != QLatin1Char('0') || count != 0)) {
        count = std::min(MaxCount, count * 10 + character.digitValue());
        showPending();
        return true;
    }
    if (pendingG) {
        pendingG = false;
        if (character == QLatin1Char('g')) executeMotion(QStringLiteral("gg"));
        else { resetPending(); emit statusMessage(tr("Expected g; command cancelled")); }
        return true;
    }
    if (character == QLatin1Char('g')) {
        pendingG = true;
        showPending();
        return true;
    }
    if (executeMotion(key)) return true;
    const int repetitions = std::max(1, count);
    const bool explicitCount = count > 0;

    if (!pendingOperator.isNull()) {
        if (character == pendingOperator) {
            const int amount = std::min<qint64>(MaxCount, qint64(repetitions) * operatorCount);
            const Position line = editor->lineFromPosition(cursor());
            operateLines(pendingOperator, line, line + amount - 1);
            if (pendingOperator == QLatin1Char('d')) remember({RepeatKind::DeleteLines, {}, amount});
        } else {
            emit statusMessage(tr("Unsupported operator motion; command cancelled"));
        }
        resetPending();
        return true;
    }
    if (mode == Mode::Visual || mode == Mode::VisualLine) {
        if (character == QLatin1Char('d') || character == QLatin1Char('x') || character == QLatin1Char('y')) {
            const bool linewise = mode == Mode::VisualLine;
            const Position start = editor->selectionStart();
            const Position end = editor->selectionEnd();
            operateRange(character == QLatin1Char('y') ? character : QLatin1Char('d'), start, end, linewise);
            // Replaying a selection safely requires shape metadata; make the
            // limitation explicit instead of repeating an older unrelated edit.
            if (character != QLatin1Char('y')) remember({});
            resetPending();
            return true;
        }
        if (character != QLatin1Char('v') && character != QLatin1Char('V')) {
            resetPending();
            emit statusMessage(tr("Use a motion, y, d, or Escape in visual mode"));
            return true;
        }
    }
    resetPending();
    if (character == QLatin1Char('d') || character == QLatin1Char('y')) {
        pendingOperator = character;
        operatorCount = repetitions;
        operatorCountExplicit = explicitCount;
        showPending();
    } else if (character == QLatin1Char('x')) {
        deleteCharacters(repetitions);
        remember({RepeatKind::DeleteCharacters, {}, repetitions});
    } else if (character == QLatin1Char('D')) {
        const Motion motion = calculateMotion(QStringLiteral("$"), repetitions, explicitCount);
        applyOperator(QLatin1Char('d'), cursor(), motion, QStringLiteral("$"), repetitions);
    } else if (character == QLatin1Char('Y')) {
        const Position line = editor->lineFromPosition(cursor());
        operateLines(QLatin1Char('y'), line, line + repetitions - 1);
    } else if (character == QLatin1Char('p') || character == QLatin1Char('P')) {
        paste(character == QLatin1Char('P'), repetitions);
    } else if (character == QLatin1Char('u')) {
        for (int i = 0; i < repetitions && editor->canUndo(); ++i) editor->undo();
        moveCursor(editor->currentPos());
    } else if (character == QLatin1Char('.')) {
        performRepeat(repetitions, explicitCount);
    } else if (QStringLiteral("iaIAoO").contains(character)) {
        if (repetitions != 1) emit statusMessage(tr("Counts on insert commands are not supported; use . to repeat"));
        else enterInsert(key);
    } else if (character == QLatin1Char('v') || character == QLatin1Char('V')) {
        const Mode target = character == QLatin1Char('v') ? Mode::Visual : Mode::VisualLine;
        const Position position = cursor();
        if (mode == target) { setMode(Mode::Normal); moveCursor(position); }
        else {
            if (mode == Mode::Normal) visualAnchor = position;
            visualCursor = position;
            setMode(target);
            refreshVisual();
        }
    } else if (character == QLatin1Char(':') || character == QLatin1Char('/') || character == QLatin1Char('?')) {
        beginPrompt(character);
    } else if (character == QLatin1Char('n') || character == QLatin1Char('N')) {
        search(character == QLatin1Char('n') ? lastSearchBackward : !lastSearchBackward, repetitions);
    } else if (character.isPrint()) {
        emit statusMessage(tr("Unsupported Vim key: %1").arg(key));
    }
    return true;
}
