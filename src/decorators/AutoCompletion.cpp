/*
 * This file is part of Notepad Next.
 * Copyright 2022 Justin Dailey
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


#include "AutoCompletion.h"
#include "NotepadNextApplication.h"
#include "LargeFilePolicy.h"

#include <QApplication>
#include <QSet>
#include <QString>
#include <Qt>


using namespace Scintilla;

AutoCompletion::AutoCompletion(ScintillaNext *editor) :
    EditorDecorator(editor)
{
    editor->autoCSetOrder(SC_ORDER_PERFORMSORT);
    editor->autoCSetIgnoreCase(true);
    editor->autoCSetCaseInsensitiveBehaviour(static_cast<sptr_t>(CaseInsensitiveBehaviour::RespectCase));
    editor->autoCSetMaxHeight(10);

    connect(editor, &ScintillaNext::lexerChanged, this, [this]() {
        NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);
        keywords = app->getLanguageKeywords(this->editor->languageName);
    });
}

void AutoCompletion::notify(const NotificationData *pscn)
{
    if (pscn->nmhdr.code == Notification::CharAdded) {
        if (editor->autoCActive())
            return;

        showAutoCompletion();
    }
}

void AutoCompletion::showManualCompletion()
{
    showCompletion(0, true);
}

void AutoCompletion::showAutoCompletion()
{
    showCompletion(3, false);
}

void AutoCompletion::showCompletion(int minimumPrefix, bool manual)
{
    int curPos = editor->currentPos();
    // wordStartPosition itself can walk a huge unbroken line, so guard before
    // asking Scintilla for word boundaries or constructing a regex.
    if (LargeFilePolicy::skipLineScan(editor->lineLength(editor->lineFromPosition(curPos))))
        return;

    int startPos = editor->wordStartPosition(curPos, true);
    int endPos = editor->wordEndPosition(curPos, true);

    const int prefix = curPos - startPos;
    if (prefix < minimumPrefix)
        return;

    const QByteArray current_word = editor->get_text_range(startPos, curPos);
    const QByteArray regex = "\\b" + current_word + "[\\w]*";
    QSet<QByteArray> words;

    editor->setSearchFlags(SCFIND_REGEXP | SCFIND_MATCHCASE);

    // Don't want to find the word that's currently being typed

    const bool scanDocument = prefix > 0 && !LargeFilePolicy::reduceAutomaticWork(editor->length()) && (!manual || prefix >= 2);
    if (scanDocument) {
        // Find everything before and after this word only for smaller buffers.
        // Large documents retain fast language-keyword completion below.
        editor->forEachMatchInRange(regex, [&](int start, int end) {
            words.insert(editor->get_text_range(start, end));
            return end;
        }, { 0, startPos});

        editor->forEachMatchInRange(regex, [&](int start, int end) {
            words.insert(editor->get_text_range(start, end));
            return end;
        }, { endPos, (Sci_PositionCR)editor->length()});
    }

    // Add matching language keywords
    const QString currentWordStr = QString::fromUtf8(current_word);
    for (const QString &kw : keywords) {
        if (prefix == 0 || kw.startsWith(currentWordStr, Qt::CaseInsensitive)) {
            words.insert(kw.toUtf8());
        }
    }

    if (!words.isEmpty()) {
        editor->autoCShow(current_word.length(), words.values().join(' '));
    }
}
