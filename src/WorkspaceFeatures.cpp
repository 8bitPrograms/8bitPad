// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorkspaceFeatures.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "EditorManager.h"
#include "ScintillaNext.h"
#include "ThemeManager.h"
#include "FolderAsWorkspaceDock.h"
#include "MarkdownPreview.h"
#include "DevOpsProfiles.h"
#include "PreferencesDialog.h"
#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace {
bool isMarkdown(ScintillaNext *editor)
{
    if (!editor) return false;
    if (editor->languageName == QStringLiteral("Markdown")) return true;
    const QString extension = QFileInfo(editor->getName()).suffix().toLower();
    return QStringList{"md", "markdown", "mdown", "mkd"}.contains(extension);
}
QString snapshot(ScintillaNext *editor)
{
    return QString::fromUtf8(editor->get_text_range(0, editor->length()));
}
}

WorkspaceFeatures::WorkspaceFeatures(MainWindow *value, NotepadNextApplication *application)
    : QObject(value), window(value), app(application), preview(new MarkdownPreview(value))
{
    setObjectName(QStringLiteral("WorkspaceFeatures"));
    window->addDockWidget(Qt::RightDockWidgetArea, preview);
    preview->hide();
    auto *settings = app->getSettings();
    QFont font;
    if (font.fromString(settings->value("MarkdownPreview/font").toString())) preview->setDocumentFont(font);
    if (font.fromString(settings->value("MarkdownPreview/codeFont").toString())) preview->setCodeFont(font);
    preview->setZoomPercent(settings->value("MarkdownPreview/zoom", 100).toInt());
    connect(preview, &MarkdownPreview::presentationChanged, this, [this] {
        auto *settings = app->getSettings();
        settings->setValue("MarkdownPreview/font", preview->textFont().toString());
        settings->setValue("MarkdownPreview/codeFont", preview->codeFont().toString());
        settings->setValue("MarkdownPreview/zoom", preview->zoomPercent());
    });
    auto *action = new QAction(tr("Markdown preview"), window);
    action->setObjectName(QStringLiteral("actionMarkdownPreview"));
    action->setShortcut(QKeySequence("Ctrl+Shift+M"));
    window->findChild<QMenu *>("menuView")->addAction(action);
    connect(action, &QAction::triggered, this, [this] { showPreview(window->currentEditor()); });
    debounce.setSingleShot(true);
    debounce.setInterval(180);
    connect(&debounce, &QTimer::timeout, this, &WorkspaceFeatures::updatePreview);
    connect(preview, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            if (!previewEditor) previewEditor = window->currentEditor();
            debounce.start();
        } else debounce.stop();
    });
    connect(window, &MainWindow::editorActivated, this, [this](ScintillaNext *editor) {
        if (preview->isVisible() && isMarkdown(editor)) {
            previewEditor = editor;
            debounce.start();
        }
    });
    const auto applyPreviewTheme = [this] {
        const auto &theme = app->getThemeManager()->currentTheme();
        preview->setSyntaxColors({theme.keyword, theme.string, theme.number, theme.comment, theme.type});
        preview->refreshTheme();
    };
    applyPreviewTheme();
    connect(app->getThemeManager(), &ThemeManager::themeChanged, preview, [this, applyPreviewTheme] {
        // Let the main window apply its palette/style sheet before rendering.
        QTimer::singleShot(0, preview, applyPreviewTheme);
    });
    connect(preview, &MarkdownPreview::openSourceRequested, this, [this](const QString &path, int line) {
        if (path.isEmpty()) {
            if (previewEditor) {
                previewEditor->gotoLine(qMax(0, line));
                previewEditor->scrollCaret();
                previewEditor->QWidget::setFocus();
            }
            return;
        }
        window->openFile(path);
        auto *editor = app->getEditorManager()->getEditorByFilePath(path);
        if (editor) {
            editor->gotoLine(qMax(0, line));
            editor->scrollCaret();
            editor->QWidget::setFocus();
        }
    });
    connect(app->getEditorManager(), &EditorManager::editorCreated, this, &WorkspaceFeatures::watchEditor);
    for (auto *editor : window->editors()) watchEditor(editor);
    if (auto *folder = window->findChild<FolderAsWorkspaceDock *>()) {
        connect(folder, &FolderAsWorkspaceDock::terminalSettingsRequested, this, [this] {
            QTimer::singleShot(0, window, [this] {
                if (auto *action = window->findChild<QAction *>("actionPreferences")) action->trigger();
                if (auto *dialog = window->findChild<PreferencesDialog *>()) dialog->showTerminalSettings();
            });
        });
        connect(folder, &FolderAsWorkspaceDock::markdownPreviewRequested, this, [this](const QString &path) {
            window->openFile(path);
            if (auto *editor = app->getEditorManager()->getEditorByFilePath(path)) showPreview(editor);
        });
        connect(folder, &FolderAsWorkspaceDock::pathRenamed, this, [this](const QString &oldPath, const QString &newPath) {
            app->getEditorManager()->followFileMove(oldPath, newPath);
        });
    }
    auto *assistance = new QAction(tr("DevOps assistanceâ€¦"), window);
    assistance->setObjectName(QStringLiteral("actionDevOpsAssistance"));
    auto *languageMenu = window->findChild<QMenu *>("menuLanguage");
    languageMenu->addSeparator();
    languageMenu->addAction(assistance);
    connect(assistance, &QAction::triggered, this, &WorkspaceFeatures::showDevOpsAssistance);
}

void WorkspaceFeatures::watchEditor(ScintillaNext *editor)
{
    if (!editor) return;
    connect(editor, &ScintillaNext::modified, this, [this, editor](Scintilla::ModificationFlags flags) {
        if (preview->isVisible() && previewEditor == editor &&
            (static_cast<int>(flags) & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) debounce.start();
    });
    connect(editor, &ScintillaNext::renamed, this, [this, editor] {
        if (previewEditor == editor && preview->isVisible()) debounce.start();
    });
    connect(editor, &ScintillaNext::closed, this, [this, editor] {
        if (previewEditor == editor) {
            previewEditor = nullptr;
            preview->setMarkdown(tr("# Markdown preview\n\nThe source document was closed."));
        }
    });
}

void WorkspaceFeatures::showPreview(ScintillaNext *editor)
{
    if (!editor) return;
    previewEditor = editor;
    preview->show();
    preview->raise();
    updatePreview();
}

void WorkspaceFeatures::updatePreview()
{
    debounce.stop();
    if (!previewEditor || !preview->isVisible()) return;
    if (previewEditor->length() > 4 * 1024 * 1024) {
        preview->setMarkdown(tr("# Preview paused\n\nMarkdown preview is limited to 4 MiB to keep editing responsive."));
    } else {
        preview->setMarkdown(snapshot(previewEditor), previewEditor->isFile()
            ? previewEditor->getFileInfo().absoluteFilePath() : QString());
    }
    preview->renderNow();
}

void WorkspaceFeatures::showDevOpsAssistance()
{
    QPointer<ScintillaNext> editor = window->currentEditor();
    if (!editor) return;
    QDialog dialog(window);
    dialog.setWindowTitle(tr("DevOps assistance"));
    dialog.resize(780, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *profiles = new QComboBox(&dialog);
    for (const auto kind : {DevOpsProfiles::Kind::GitHubActions, DevOpsProfiles::Kind::AzurePipelines,
                           DevOpsProfiles::Kind::Ansible, DevOpsProfiles::Kind::Jenkins})
        profiles->addItem(DevOpsProfiles::languageName(kind), static_cast<int>(kind));
    auto current = DevOpsProfiles::kindForLanguage(editor->languageName);
    if (current == DevOpsProfiles::Kind::None && editor->isFile())
        current = DevOpsProfiles::detect(editor->getFileInfo().absoluteFilePath(),
            QString::fromUtf8(editor->get_text_range(0, qMin<sptr_t>(editor->length(), 64 * 1024))));
    profiles->setCurrentIndex(qMax(0, profiles->findData(static_cast<int>(current))));
    layout->addWidget(profiles);
    auto *description = new QLabel(tr("Choose a profile for keywords, templates and checks. Templates are inserted at the cursor; commands are never executed."), &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *split = new QSplitter(&dialog);
    auto *templates = new QListWidget(split);
    templates->setAccessibleName(tr("Templates"));
    auto *code = new QPlainTextEdit(split);
    code->setReadOnly(true);
    code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    code->setAccessibleName(tr("Template preview"));
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 2);
    auto *results = new QListWidget(&dialog);
    results->setAccessibleName(tr("Profile check results"));
    layout->addWidget(results, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *insert = buttons->addButton(tr("Insert template"), QDialogButtonBox::ActionRole);
    auto *apply = buttons->addButton(tr("Use profile"), QDialogButtonBox::ActionRole);
    auto *check = buttons->addButton(tr("Check document"), QDialogButtonBox::ActionRole);
    auto *docs = buttons->addButton(tr("Documentation"), QDialogButtonBox::HelpRole);
    layout->addWidget(buttons);
    QList<DevOpsProfiles::Snippet> snippets;
    const auto selected = [profiles] { return static_cast<DevOpsProfiles::Kind>(profiles->currentData().toInt()); };
    const auto refresh = [&] {
        snippets = DevOpsProfiles::snippets(selected());
        templates->clear();
        results->clear();
        for (const auto &snippet : snippets) templates->addItem(snippet.title);
        if (templates->count()) templates->setCurrentRow(0);
        check->setEnabled(selected() != DevOpsProfiles::Kind::Jenkins && editor);
        results->addItem(selected() == DevOpsProfiles::Kind::Jenkins
            ? tr("Jenkinsfile uses Groovy. Templates and highlighting are available; syntax validation belongs to Jenkins.")
            : tr("Check document looks for selected common YAML mistakes. It does not validate the complete service schema."));
    };
    connect(templates, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        code->setPlainText(row >= 0 && row < snippets.size() ? snippets[row].text : QString());
        insert->setEnabled(editor && row >= 0);
    });
    connect(profiles, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(apply, &QPushButton::clicked, &dialog, [&] {
        if (editor) app->setEditorLanguage(editor, DevOpsProfiles::languageName(selected()));
    });
    connect(insert, &QPushButton::clicked, &dialog, [&] {
        const int row = templates->currentRow();
        if (!editor || row < 0 || row >= snippets.size()) return;
        QByteArray text = snippets[row].text.toUtf8();
        text.replace("\n", editor->eolString());
        editor->beginUndoAction();
        editor->replaceSel(text.constData());
        editor->endUndoAction();
        dialog.accept();
    });
    connect(check, &QPushButton::clicked, &dialog, [&] {
        if (!editor) return;
        results->clear();
        if (editor->length() > 1024 * 1024) {
            results->addItem(tr("Checks are limited to documents up to 1 MiB."));
            return;
        }
        const auto diagnostics = DevOpsProfiles::check(selected(), snapshot(editor));
        for (const auto &diagnostic : diagnostics) {
            auto *item = new QListWidgetItem(tr("Line %1: %2").arg(diagnostic.line).arg(diagnostic.message), results);
            item->setData(Qt::UserRole, diagnostic.line);
        }
        if (diagnostics.isEmpty()) results->addItem(tr("No issues found by these limited checks."));
    });
    connect(results, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem *item) {
        if (editor && item->data(Qt::UserRole).isValid()) {
            editor->gotoLine(qMax(0, item->data(Qt::UserRole).toInt() - 1));
            editor->scrollCaret();
        }
    });
    connect(docs, &QPushButton::clicked, &dialog, [&] {
        QDesktopServices::openUrl(QUrl(DevOpsProfiles::documentationUrl(selected())));
    });
    refresh();
    dialog.exec();
}
