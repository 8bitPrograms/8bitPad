// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompareView.h"

#include "DockWidget.h"
#include "LineDiff.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "ThemeManager.h"

#include <QDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBlock>
#include <QVBoxLayout>

namespace {
QList<QPointer<ScintillaNext>> picked;

QString documentText(ScintillaNext *editor, bool *truncated)
{
    const int cap = 1500000;
    const auto length = editor->length();
    const bool shorten = length > cap;
    if (truncated)
        *truncated = shorten;
    const int count = shorten ? cap : static_cast<int>(length);
    return QString::fromUtf8(editor->get_text_range(0, count));
}

int indexOf(ScintillaNext *editor)
{
    for (int i = 0; i < picked.size(); ++i) {
        if (picked.at(i) == editor)
            return i;
    }
    return -1;
}

void refreshTitle(ScintillaNext *editor)
{
    if (!editor)
        return;
    auto *dock = qobject_cast<ads::CDockWidget *>(editor->parentWidget());
    if (!dock)
        return;
    QString title = editor->getName();
    if (indexOf(editor) >= 0)
        title.prepend(QStringLiteral("● "));
    dock->setWindowTitle(title);
}

class CompareDialog final : public QDialog
{
public:
    CompareDialog(const QString &leftName, const QString &leftText, const QString &rightName, const QString &rightText,
                  const QColor &background, const QColor &foreground, bool shortened, QWidget *parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("compareDialog"));
        setWindowTitle(tr("Compare"));
        resize(1100, 680);
        setAttribute(Qt::WA_DeleteOnClose);
        auto *layout = new QVBoxLayout(this);
        const DiffResult diff = diffLines(splitLines(leftText), splitLines(rightText));
        if (shortened || diff.truncated) {
            auto *note = new QLabel(shortened
                ? tr("Showing the first 1.5 MB of a large file, and at most 5,000 lines.")
                : tr("Showing the first 5,000 lines of each file."), this);
            note->setWordWrap(true);
            layout->addWidget(note);
        }
        auto *splitter = new QSplitter(this);
        auto *leftPane = makePane(leftName, background, foreground);
        auto *rightPane = makePane(rightName, background, foreground);
        splitter->addWidget(leftPane);
        splitter->addWidget(rightPane);
        layout->addWidget(splitter);

        QString leftBody;
        QString rightBody;
        QList<DiffRow::Kind> kinds;
        for (const DiffRow &row : diff.rows) {
            leftBody.append(row.kind == DiffRow::Insert ? QStringLiteral(" \n") : row.left + QLatin1Char('\n'));
            rightBody.append(row.kind == DiffRow::Delete ? QStringLiteral(" \n") : row.right + QLatin1Char('\n'));
            kinds.append(row.kind);
        }
        leftPane->findChild<QPlainTextEdit *>()->setPlainText(leftBody);
        rightPane->findChild<QPlainTextEdit *>()->setPlainText(rightBody);
        paint(leftPane->findChild<QPlainTextEdit *>(), kinds, true);
        paint(rightPane->findChild<QPlainTextEdit *>(), kinds, false);
        sync(leftPane->findChild<QPlainTextEdit *>(), rightPane->findChild<QPlainTextEdit *>());
    }

private:
    static QWidget *makePane(const QString &name, const QColor &background, const QColor &foreground)
    {
        auto *pane = new QWidget;
        auto *layout = new QVBoxLayout(pane);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *title = new QLabel(name, pane);
        title->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *edit = new QPlainTextEdit(pane);
        edit->setReadOnly(true);
        edit->setLineWrapMode(QPlainTextEdit::NoWrap);
        edit->setFrameStyle(QFrame::NoFrame);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (font.pointSize() > 0)
            font.setPointSize(qMax(10, font.pointSize()));
        edit->setFont(font);
        QPalette palette = edit->palette();
        palette.setColor(QPalette::Base, background);
        palette.setColor(QPalette::Text, foreground);
        edit->setPalette(palette);
        layout->addWidget(title);
        layout->addWidget(edit);
        return pane;
    }

    static void paint(QPlainTextEdit *edit, const QList<DiffRow::Kind> &kinds, bool left)
    {
        QList<QTextEdit::ExtraSelection> marks;
        QTextBlock block = edit->document()->firstBlock();
        for (int row = 0; row < kinds.size() && block.isValid(); ++row, block = block.next()) {
            const DiffRow::Kind kind = kinds.at(row);
            QColor color;
            if (left && (kind == DiffRow::Delete || kind == DiffRow::Change))
                color = QColor(196, 64, 64, 78);
            else if (!left && (kind == DiffRow::Insert || kind == DiffRow::Change))
                color = QColor(46, 140, 78, 78);
            else
                continue;
            QTextEdit::ExtraSelection mark;
            mark.cursor = QTextCursor(block);
            mark.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            mark.format.setBackground(color);
            mark.format.setProperty(QTextFormat::FullWidthSelection, true);
            marks.append(mark);
        }
        edit->setExtraSelections(marks);
    }

    static void sync(QPlainTextEdit *left, QPlainTextEdit *right)
    {
        auto bind = [](QPlainTextEdit *from, QPlainTextEdit *to) {
            QObject::connect(from->verticalScrollBar(), &QScrollBar::valueChanged, to, [to](int value) {
                if (to->verticalScrollBar()->value() != value)
                    to->verticalScrollBar()->setValue(value);
            });
            QObject::connect(from->horizontalScrollBar(), &QScrollBar::valueChanged, to, [to](int value) {
                if (to->horizontalScrollBar()->value() != value)
                    to->horizontalScrollBar()->setValue(value);
            });
        };
        bind(left, right);
        bind(right, left);
    }
};

void showCompare(MainWindow *window, NotepadNextApplication *app)
{
    QVector<ScintillaNext *> editors;
    for (const auto &editor : picked) {
        if (editor)
            editors.append(editor.data());
    }
    if (editors.size() != 2)
        return;
    const auto &theme = app->getThemeManager()->currentTheme();
    bool leftShort = false;
    bool rightShort = false;
    const QString leftText = documentText(editors.at(0), &leftShort);
    const QString rightText = documentText(editors.at(1), &rightShort);
    auto *dialog = new CompareDialog(editors.at(0)->getName(), leftText,
                                     editors.at(1)->getName(), rightText,
                                     theme.background, theme.foreground, leftShort || rightShort, window);
    dialog->show();
}
}

void toggleCompareSelection(ScintillaNext *editor)
{
    if (!editor)
        return;
    const int existing = indexOf(editor);
    if (existing >= 0) {
        picked.removeAt(existing);
    } else {
        if (picked.size() == 2) {
            ScintillaNext *oldest = picked.takeFirst().data();
            refreshTitle(oldest);
        }
        picked.append(editor);
    }
    refreshTitle(editor);
    if (auto *window = qobject_cast<MainWindow *>(editor->window())) {
        prepareCompareMenu(editor);
        QStringList names;
        for (const auto &item : picked)
            if (item)
                names.append(item->getName());
        window->statusBar()->showMessage(names.isEmpty()
            ? MainWindow::tr("Compare selection cleared")
            : MainWindow::tr("Compare selection: %1").arg(names.join(MainWindow::tr(" and "))), 5000);
    }
}

void prepareCompareMenu(ScintillaNext *editor)
{
    if (!editor)
        return;
    auto *window = qobject_cast<MainWindow *>(editor->window());
    if (!window)
        return;
    picked.removeIf([](const QPointer<ScintillaNext> &item) { return item.isNull(); });
    if (auto *select = window->findChild<QAction *>(QStringLiteral("actionSelectForCompare")))
        select->setChecked(indexOf(editor) >= 0);
    if (auto *compare = window->findChild<QAction *>(QStringLiteral("actionCompareSelected"))) {
        compare->setEnabled(picked.size() == 2);
        if (picked.size() == 2 && picked.at(0) && picked.at(1))
            compare->setText(MainWindow::tr("Compare \"%1\" with \"%2\"").arg(picked.at(0)->getName(), picked.at(1)->getName()));
        else
            compare->setText(MainWindow::tr("Compare"));
    }
    if (auto *clear = window->findChild<QAction *>(QStringLiteral("actionClearCompare")))
        clear->setEnabled(!picked.isEmpty());
}

void installCompare(MainWindow *window, NotepadNextApplication *app)
{
    auto *select = new QAction(MainWindow::tr("Select for Compare"), window);
    select->setObjectName(QStringLiteral("actionSelectForCompare"));
    select->setCheckable(true);
    auto *compare = new QAction(MainWindow::tr("Compare"), window);
    compare->setObjectName(QStringLiteral("actionCompareSelected"));
    compare->setEnabled(false);
    auto *clear = new QAction(MainWindow::tr("Clear Compare Selection"), window);
    clear->setObjectName(QStringLiteral("actionClearCompare"));
    clear->setEnabled(false);
    if (auto *view = window->findChild<QMenu *>(QStringLiteral("menuView"))) {
        view->addSeparator();
        view->addAction(select);
        view->addAction(compare);
        view->addAction(clear);
    }
    QObject::connect(select, &QAction::triggered, window, [window] {
        toggleCompareSelection(window->currentEditor());
    });
    QObject::connect(window, &MainWindow::editorActivated, window, [](ScintillaNext *editor) {
        prepareCompareMenu(editor);
    });
    QObject::connect(compare, &QAction::triggered, window, [window, app] { showCompare(window, app); });
    QObject::connect(clear, &QAction::triggered, window, [window] {
        const auto previous = picked;
        picked.clear();
        for (const auto &editor : previous)
            refreshTitle(editor.data());
        prepareCompareMenu(window->currentEditor());
        window->statusBar()->showMessage(MainWindow::tr("Compare selection cleared"), 3000);
    });
    QObject::connect(window->getDockedEditor(), &DockedEditor::editorClosed, window, [](ScintillaNext *editor) {
        const int existing = indexOf(editor);
        if (existing >= 0)
            picked.removeAt(existing);
    });
    auto watchTitle = [](ScintillaNext *editor) {
        if (!editor)
            return;
        QObject::connect(editor, &ScintillaNext::renamed, editor, [editor] { refreshTitle(editor); }, Qt::QueuedConnection);
    };
    QObject::connect(window->getDockedEditor(), &DockedEditor::editorAdded, window, watchTitle);
    for (auto *editor : window->editors())
        watchTitle(editor);
}
