/* Copyright 2026 8bitPad contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "VimMode.h"
#include "ScintillaEdit.h"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QtTest>
#include <memory>

class VimModeTest : public QObject
{
    Q_OBJECT
private:
    std::unique_ptr<ScintillaEdit> editor;
    VimMode *vim = nullptr;

    void load(const QByteArray &text, int eol = SC_EOL_LF)
    {
        editor->setText(text.constData());
        editor->setEOLMode(eol);
        editor->emptyUndoBuffer();
        editor->setSel(0, 0);
    }
    QByteArray content() const { return editor->textRange(0, editor->length()); }
    void keys(const char *text) { QTest::keyClicks(editor.get(), QString::fromLatin1(text)); }
    void escape() { QTest::keyClick(editor.get(), Qt::Key_Escape); }
    void text(const QString &value)
    {
        QKeyEvent event(QEvent::KeyPress, 0, Qt::NoModifier, value);
        QCoreApplication::sendEvent(editor.get(), &event);
    }

private slots:
    void init()
    {
        editor = std::make_unique<ScintillaEdit>();
        editor->setCodePage(SC_CP_UTF8);
        editor->setCaretStyle(CARETSTYLE_LINE);
        vim = new VimMode(editor.get());
        load({});
        vim->setEnabled(true);
    }
    void cleanup() { editor.reset(); vim = nullptr; }

    void disabledModePassesThroughAndRestoresCaret()
    {
        QCOMPARE(editor->caretStyle(), sptr_t(CARETSTYLE_BLOCK));
        vim->setEnabled(false);
        QCOMPARE(editor->caretStyle(), sptr_t(CARETSTYLE_LINE));
        keys("plain text");
        QCOMPARE(content(), QByteArray("plain text"));
        vim->setEnabled(true);
        keys("l");
        QCOMPARE(content(), QByteArray("plain text"));
        QCOMPARE(vim->modeName(), QString("NORMAL"));
    }

    void insertSessionIsOneUndoAction()
    {
        keys("ihello");
        QCOMPARE(vim->modeName(), QString("INSERT"));
        escape();
        QCOMPARE(content(), QByteArray("hello"));
        QCOMPARE(editor->currentPos(), sptr_t(4));
        keys("u");
        QVERIFY(content().isEmpty());
        QTest::keyClick(editor.get(), Qt::Key_R, Qt::ControlModifier);
        QCOMPARE(content(), QByteArray("hello"));
    }

    void utf8MotionsAndDeletionNeverSplitCharacters()
    {
        load(QString::fromUtf8("ağ🙂z").toUtf8());
        keys("l");
        QCOMPARE(editor->currentPos(), sptr_t(1));
        keys("l");
        QCOMPARE(editor->currentPos(), sptr_t(3));
        keys("x");
        QCOMPARE(content(), QString::fromUtf8("ağz").toUtf8());
        keys("u");
        QCOMPARE(content(), QString::fromUtf8("ağ🙂z").toUtf8());
        keys("0v2ly");
        keys("$p");
        QCOMPARE(content(), QString::fromUtf8("ağ🙂zağ🙂").toUtf8());
    }

    void unicodeWordMotions()
    {
        load(QString::fromUtf8("çağrı iki,üç").toUtf8());
        keys("w");
        QCOMPARE(editor->currentPos(), sptr_t(QString::fromUtf8("çağrı ").toUtf8().size()));
        keys("e");
        QCOMPARE(editor->charAt(editor->currentPos()), sptr_t('i'));
        keys("b");
        QCOMPARE(editor->charAt(editor->currentPos()), sptr_t('i'));
        keys("0e");
        QCOMPARE(editor->currentPos(), sptr_t(QString::fromUtf8("çağr").toUtf8().size()));
    }

    void operatorAndMotionCountsMultiply()
    {
        load("one two three four five six\n");
        keys("2d2w");
        QCOMPARE(content(), QByteArray("five six\n"));
        keys("u");
        QCOMPARE(content(), QByteArray("one two three four five six\n"));
    }

    void lineCountsAndSingleUndo()
    {
        load("one\ntwo\nthree\nfour");
        keys("2dd");
        QCOMPARE(content(), QByteArray("three\nfour"));
        keys("u");
        QCOMPARE(content(), QByteArray("one\ntwo\nthree\nfour"));
        keys("3G");
        QCOMPARE(editor->lineFromPosition(editor->currentPos()), sptr_t(2));
        keys("gg");
        QCOMPARE(editor->currentPos(), sptr_t(0));
        keys("G");
        QCOMPARE(editor->lineFromPosition(editor->currentPos()), sptr_t(3));
    }

    void linePasteAndLastLineDeletion()
    {
        load("one\ntwo\nthree");
        keys("yyp");
        QCOMPARE(content(), QByteArray("one\none\ntwo\nthree"));
        keys("Gdd");
        QCOMPARE(content(), QByteArray("one\none\ntwo"));
        keys("p");
        QCOMPARE(content(), QByteArray("one\none\ntwo\nthree"));
    }

    void gotoLineOperatorDistinguishesExplicitOne()
    {
        load("one\ntwo\nthree\nfour");
        keys("j1dG");
        QCOMPARE(content(), QByteArray("three\nfour"));
        keys("u");
        keys("ggjdG");
        QCOMPARE(content(), QByteArray("one"));
        keys("u");
        keys("gg.");
        QVERIFY(content().isEmpty());
    }

    void deletingTerminatedLinePreservesPreviousEol()
    {
        load("one\ntwo\n");
        keys("jdd");
        QCOMPARE(content(), QByteArray("one\n"));
        keys("u");
        QCOMPARE(content(), QByteArray("one\ntwo\n"));
    }

    void windowsEolStaysIntact()
    {
        load("one\r\ntwo\r\nthree", SC_EOL_CRLF);
        keys("jdd");
        QCOMPARE(content(), QByteArray("one\r\nthree"));
        keys("P");
        QCOMPARE(content(), QByteArray("one\r\ntwo\r\nthree"));
        keys("A!");
        escape();
        QCOMPARE(content(), QByteArray("one\r\ntwo!\r\nthree"));
    }

    void wordDeleteAtLineEndRetainsLineEnding()
    {
        load("one\ntwo");
        keys("dw");
        QCOMPARE(content(), QByteArray("\ntwo"));
        keys("u$d$");
        QCOMPARE(content(), QByteArray("on\ntwo"));
    }

    void visualEmptyLineDoesNotSplitCrLf()
    {
        load("\r\nnext", SC_EOL_CRLF);
        keys("vd");
        QCOMPARE(content(), QByteArray("next"));
        keys("u");
        QCOMPARE(content(), QByteArray("\r\nnext"));
    }

    void visualCharacterAndLineOperations()
    {
        load("abcd\nefgh\nijkl");
        keys("v2ld");
        QCOMPARE(content(), QByteArray("d\nefgh\nijkl"));
        keys("u");
        keys("ggVjy");
        QCOMPARE(vim->modeName(), QString("NORMAL"));
        keys("Gp");
        QCOMPARE(content(), QByteArray("abcd\nefgh\nijkl\nabcd\nefgh"));
    }

    void escapeCancelsPendingOperatorAndCount()
    {
        load("abcd\nefgh");
        keys("3d");
        escape();
        keys("l");
        QCOMPARE(editor->currentPos(), sptr_t(1));
        QCOMPARE(content(), QByteArray("abcd\nefgh"));
        keys("vll");
        escape();
        QCOMPARE(editor->selectionStart(), editor->selectionEnd());
        QCOMPARE(editor->currentPos(), sptr_t(3));
    }

    void insertAndDeleteRepeat()
    {
        keys("ihello");
        escape();
        keys("A!");
        escape();
        keys(".");
        QCOMPARE(content(), QByteArray("hello!!"));
        keys("u");
        QCOMPARE(content(), QByteArray("hello!"));
        keys("0x.");
        QCOMPARE(content(), QByteArray("llo!"));
        keys("u");
        QCOMPARE(content(), QByteArray("ello!"));
    }

    void openLineAndRepeatAreAtomic()
    {
        load("one\ntwo");
        keys("onew");
        escape();
        QCOMPARE(content(), QByteArray("one\nnew\ntwo"));
        keys(".");
        QCOMPARE(content(), QByteArray("one\nnew\nnew\ntwo"));
        keys("u");
        QCOMPARE(content(), QByteArray("one\nnew\ntwo"));
        keys("u");
        QCOMPARE(content(), QByteArray("one\ntwo"));
    }

    void unsupportedInsertCountDoesNotEdit()
    {
        load("one");
        QSignalSpy status(vim, &VimMode::statusMessage);
        keys("3i");
        QCOMPARE(content(), QByteArray("one"));
        QCOMPARE(vim->modeName(), QString("NORMAL"));
        QVERIFY(status.last().at(0).toString().contains("not supported"));
    }

    void literalSearchWrapsAndRepeats()
    {
        load("one two one two");
        keys("/two");
        QTest::keyClick(editor.get(), Qt::Key_Return);
        QCOMPARE(editor->currentPos(), sptr_t(4));
        keys("n");
        QCOMPARE(editor->currentPos(), sptr_t(12));
        keys("n");
        QCOMPARE(editor->currentPos(), sptr_t(4));
        keys("N");
        QCOMPARE(editor->currentPos(), sptr_t(12));
        keys("?one");
        QTest::keyClick(editor.get(), Qt::Key_Return);
        QCOMPARE(editor->currentPos(), sptr_t(8));
        keys("n");
        QCOMPARE(editor->currentPos(), sptr_t(0));
    }

    void searchDoesNotOverwriteScintillaFindState()
    {
        load("one two");
        editor->setSearchFlags(SCFIND_REGEXP | SCFIND_MATCHCASE);
        editor->setTargetRange(1, 3);
        keys("/two");
        QTest::keyClick(editor.get(), Qt::Key_Return);
        QCOMPARE(editor->searchFlags(), sptr_t(SCFIND_REGEXP | SCFIND_MATCHCASE));
        QCOMPARE(editor->targetStart(), sptr_t(1));
        QCOMPARE(editor->targetEnd(), sptr_t(3));
    }

    void searchWrapCanReturnToOnlyMatch()
    {
        load("one two");
        QSignalSpy status(vim, &VimMode::statusMessage);
        keys("/one");
        QTest::keyClick(editor.get(), Qt::Key_Return);
        QCOMPARE(editor->currentPos(), sptr_t(0));
        QCOMPARE(status.last().at(0).toString(), QString("Search wrapped"));
        keys("n");
        QCOMPARE(editor->currentPos(), sptr_t(0));
        QCOMPARE(status.last().at(0).toString(), QString("Search wrapped"));
    }

    void unicodeSearchPrompt()
    {
        load(QString::fromUtf8("one çağrı").toUtf8());
        keys("/");
        text(QString::fromUtf8("çağrı"));
        QTest::keyClick(editor.get(), Qt::Key_Return);
        QCOMPARE(editor->currentPos(), sptr_t(4));
    }

    void exCommandsAreSignalsAndUnknownCommandsAreRejected()
    {
        load("unsaved");
        QSignalSpy commands(vim, &VimMode::exCommand);
        QSignalSpy statuses(vim, &VimMode::statusMessage);
        for (const char *command : {":w", ":q", ":wq", ":q!"}) {
            keys(command);
            QTest::keyClick(editor.get(), Qt::Key_Return);
        }
        QCOMPARE(commands.count(), 4);
        QCOMPARE(commands.at(0).at(0).toString(), QString("w"));
        QCOMPARE(commands.at(3).at(0).toString(), QString("q!"));
        keys(":!danger");
        QTest::keyClick(editor.get(), Qt::Key_Return);
        QCOMPARE(commands.count(), 4);
        QVERIFY(statuses.last().at(0).toString().contains("Unsupported Vim command"));
        QCOMPARE(content(), QByteArray("unsaved"));
    }

    void readOnlyBlocksModalMutations()
    {
        load("one\ntwo");
        editor->setReadOnly(true);
        keys("ddix");
        QCOMPARE(content(), QByteArray("one\ntwo"));
        QCOMPARE(vim->modeName(), QString("NORMAL"));
        editor->setReadOnly(false);
    }

    void inputMethodCannotInsertInNormalMode()
    {
        load("one");
        QInputMethodEvent event;
        event.setCommitString(QString::fromUtf8("ğ"));
        QCoreApplication::sendEvent(editor.get(), &event);
        QCOMPARE(content(), QByteArray("one"));
    }

    void deletingEditorDuringInsertIsSafe()
    {
        keys("iunfinished");
        editor.reset();
        vim = nullptr;
    }
};

QTEST_MAIN(VimModeTest)
#include "VimModeTest.moc"
