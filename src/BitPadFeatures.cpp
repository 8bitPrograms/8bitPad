// SPDX-License-Identifier: GPL-3.0-or-later
#include "BitPadFeatures.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "EditorManager.h"
#include "ThemeManager.h"
#include "VimMode.h"
#include "FolderSearchDialog.h"
#include "ShortcutDialog.h"
#include "CompareView.h"
#include "TakeNotes.h"
#include "WorkspaceFeatures.h"
#include "ShellIntegration.h"
#include "Readability.h"
#include "FolderAsWorkspaceDock.h"
#include "AutoCompletion.h"
#include <QDir>
#include <QInputDialog>
#include <QMessageBox>
#include <QActionGroup>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QStatusBar>
#include <QVBoxLayout>

BitPadFeatures::BitPadFeatures(MainWindow *window, NotepadNextApplication *app)
    : QObject(window), window(window), app(app)
{
    auto *settingsMenu = window->findChild<QMenu *>("menuSettings");
    auto *themeMenu = settingsMenu->addMenu(tr("Color theme"));
    themeMenu->setObjectName("menuColorTheme");
    auto *darkMenu = themeMenu->addMenu(tr("Dark"));
    auto *lightMenu = themeMenu->addMenu(tr("Light"));
    auto *themeGroup = new QActionGroup(this);
    for (const auto &theme : ThemeManager::catalog()) {
        QAction *action = (theme.isDark ? darkMenu : lightMenu)->addAction(theme.name);
        action->setObjectName("theme_" + theme.id);
        action->setCheckable(true);
        action->setData(theme.id);
        action->setChecked(theme.id == app->getThemeManager()->currentThemeId());
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [app, id = theme.id] { app->getThemeManager()->setTheme(id); });
    }
    connect(app->getThemeManager(), &ThemeManager::themeChanged, this, [this, themeGroup] {
        for (auto *action : themeGroup->actions())
            action->setChecked(action->data().toString() == this->app->getThemeManager()->currentThemeId());
    });

    settingsMenu->addSeparator();
    vimAction = new QAction(tr("Vim mode"), window);
    vimAction->setObjectName("actionVimMode");
    vimAction->setCheckable(true);
    vimAction->setShortcut(QKeySequence("Ctrl+Alt+V"));
    vimAction->setChecked(app->getSettings()->value("Editor/VimMode", false).toBool());
    settingsMenu->addAction(vimAction);
    modeLabel = new QLabel(window);
    modeLabel->setObjectName("vimModeLabel");
    modeLabel->setMinimumWidth(76);
    modeLabel->setAlignment(Qt::AlignCenter);
    modeLabel->setToolTip(tr("Vim mode: Ctrl+Alt+V. Escape returns to Normal mode."));
    promptLabel = new QLabel(window);
    promptLabel->setObjectName("vimPromptLabel");
    promptLabel->setMinimumWidth(130);
    window->statusBar()->addPermanentWidget(promptLabel);
    window->statusBar()->addPermanentWidget(modeLabel);
    connect(vimAction, &QAction::toggled, this, [this](bool enabled) {
        this->app->getSettings()->setValue("Editor/VimMode", enabled);
        for (auto *editor : this->window->editors())
            if (editor) if (auto *vim = editor->findChild<VimMode *>()) vim->setEnabled(enabled);
        updateMode();
    });
    connect(app->getEditorManager(), &EditorManager::editorCreated, this, &BitPadFeatures::attachEditor);
    connect(window, &MainWindow::editorActivated, this, &BitPadFeatures::updateMode);
    for (auto *editor : window->editors()) attachEditor(editor);
    updateMode();

    auto *paletteAction = new QAction(tr("Command palette…"), window);
    paletteAction->setObjectName("actionCommandPalette");
    paletteAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
    settingsMenu->addAction(paletteAction);
    connect(paletteAction, &QAction::triggered, this, &BitPadFeatures::showCommandPalette);
    auto *shortcutsAction = new QAction(tr("Keyboard shortcuts…"), window);
    shortcutsAction->setObjectName("actionKeyboardShortcuts");
    settingsMenu->addAction(shortcutsAction);
    connect(shortcutsAction, &QAction::triggered, this, [window, app] { showShortcutDialog(window, app->getSettings()); });

    auto *searchAction = new QAction(tr("Find / replace in files…"), window);
    searchAction->setObjectName("actionFindInFiles");
    searchAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
    window->findChild<QMenu *>("menuSearch")->addAction(searchAction);
    auto *folderSearch = new FolderSearchDialog(window);
    folderSearch->setObjectName("folderSearchDialog");
    auto refreshOpenFiles = [window, folderSearch] {
        QStringList paths;
        for (auto *editor : window->editors()) if (editor->isFile()) paths << editor->getFilePath();
        folderSearch->setOpenFiles(paths);
    };
    connect(searchAction, &QAction::triggered, this, [window, folderSearch, refreshOpenFiles] {
        refreshOpenFiles();
        auto *editor = window->currentEditor();
        folderSearch->setDirectory(editor && editor->isFile() ? editor->getFileInfo().absolutePath() : QDir::homePath());
        folderSearch->show();
        folderSearch->raise();
        folderSearch->activateWindow();
    });
    connect(folderSearch, &FolderSearchDialog::aboutToReplace, this, refreshOpenFiles);
    connect(folderSearch, &FolderSearchDialog::openResult, this, [window, refreshOpenFiles](const QString &path, int line, int column) {
        window->openFile(path);
        auto *editor = window->currentEditor();
        if (editor && editor->getFileInfo().absoluteFilePath() == QFileInfo(path).absoluteFilePath()) {
            const auto lineStart = editor->positionFromLine(qMax(0, line - 1));
            editor->gotoPos(editor->positionRelativeCodeUnits(lineStart, qMax(0, column - 1)));
            editor->scrollCaret();
            editor->QWidget::setFocus();
        }
        refreshOpenFiles();
    });

    auto *encodingMenu = window->findChild<QMenu *>("menuEncodings");
    encodingMenu->clear();
    auto *convertAction = encodingMenu->addAction(tr("Convert encoding…"));
    convertAction->setObjectName("actionConvertEncoding");
    connect(convertAction, &QAction::triggered, this, [window] {
        auto *editor = window->currentEditor();
        if (!editor) return;
        const auto codecs = ScintillaNext::availableEncodings();
        bool ok = false;
        const QString chosen = QInputDialog::getItem(window, tr("Convert encoding"),
            tr("Current: %1. Conversion is written when you save.").arg(editor->encodingName()),
            codecs, qMax(0, codecs.indexOf(editor->encodingName())), false, &ok);
        if (ok && !editor->setEncoding(chosen))
            QMessageBox::warning(window, tr("Encoding"), tr("This encoding is not available."));
    });
    auto *reopenAction = encodingMenu->addAction(tr("Reopen using encoding…"));
    reopenAction->setObjectName("actionReopenEncoding");
    connect(reopenAction, &QAction::triggered, this, [window] {
        auto *editor = window->currentEditor();
        if (!editor || !editor->isFile()) return;
        if (!editor->isSavedToDisk() && QMessageBox::question(window, tr("Reopen file"),
            tr("Reopening discards the current unsaved changes. Continue?"), QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) return;
        bool ok = false;
        const QString chosen = QInputDialog::getItem(window, tr("Reopen using encoding"),
            tr("Interpret the file's bytes as:"), ScintillaNext::availableEncodings(), 0, false, &ok);
        if (ok && !editor->reloadWithEncoding(chosen))
            QMessageBox::warning(window, tr("Reopen failed"), tr("The file could not be decoded using this encoding. Your current buffer was kept."));
    });

    new WorkspaceFeatures(window, app);
    installCompare(window, app);
    installTakeNotes(window, app);
    installShellIntegration(window);
    if (auto *folder = window->findChild<FolderAsWorkspaceDock *>()) {
        auto *gitLabel = new QLabel(window);
        gitLabel->setObjectName(QStringLiteral("gitBranchLabel"));
        gitLabel->setMinimumWidth(64);
        gitLabel->setAlignment(Qt::AlignCenter);
        gitLabel->hide();
        window->statusBar()->addPermanentWidget(gitLabel);
        connect(folder, &FolderAsWorkspaceDock::gitStatusChanged, gitLabel, [gitLabel](const QString &text, const QString &tip) {
            gitLabel->setText(text);
            gitLabel->setToolTip(tip);
            gitLabel->setVisible(!text.isEmpty());
        });
        auto applyGitColors = [folder, app] {
            const auto &theme = app->getThemeManager()->currentTheme();
            const QColor paper = theme.background;
            folder->setGitColors(liftContrast(theme.error, paper, 4.5),
                                 liftContrast(theme.string, paper, 4.5),
                                 liftContrast(theme.accent, paper, 4.5),
                                 liftContrast(theme.comment, paper, 4.5));
        };
        applyGitColors();
        connect(app->getThemeManager(), &ThemeManager::themeChanged, folder, [applyGitColors](const QString &) { applyGitColors(); });
        auto watchSave = [folder](ScintillaNext *editor) {
            if (!editor) return;
            connect(editor, &ScintillaNext::savePointChanged, folder, [folder](bool) { folder->refreshGit(); });
        };
        connect(app->getEditorManager(), &EditorManager::editorCreated, folder, watchSave);
        for (auto *editor : window->editors())
            watchSave(editor);
    }
    auto *complete = new QAction(tr("Complete word"), window);
    complete->setObjectName(QStringLiteral("actionCompleteWord"));
    complete->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    complete->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    window->addAction(complete);
    if (auto *edit = window->findChild<QMenu *>(QStringLiteral("menuEdit")))
        edit->addAction(complete);
    connect(complete, &QAction::triggered, window, [window] {
        auto *editor = window->currentEditor();
        if (!editor) return;
        if (auto *completion = editor->findChild<AutoCompletion *>())
            completion->showManualCompletion();
    });
}

void BitPadFeatures::attachEditor(ScintillaNext *editor)
{
    if (editor->findChild<VimMode *>()) return;
    auto *vim = new VimMode(editor, editor);
    vim->setObjectName("VimMode");
    vim->setEnabled(vimAction->isChecked());
    connect(vim, &VimMode::modeChanged, this, [this, editor] { if (window->currentEditor() == editor) updateMode(); });
    connect(vim, &VimMode::statusMessage, this, [this, editor](const QString &message) {
        if (window->currentEditor() == editor) {
            promptLabel->setText(message);
            promptLabel->setToolTip(message);
        }
    });
    connect(vim, &VimMode::exCommand, this, [this, guarded = QPointer<ScintillaNext>(editor)](const QString &command) {
        auto *editor = guarded.data();
        if (!editor) return;
        if (command == "w") window->saveFile(editor);
        else if (command == "wq") { if (window->saveFile(editor)) window->closeFile(editor); }
        else if (command == "q") window->closeFile(editor);
        else if (command == "q!") window->closeFile(editor, true);
    }, Qt::QueuedConnection);
}

void BitPadFeatures::updateMode()
{
    promptLabel->clear();
    auto *editor = window->currentEditor();
    auto *vim = editor ? editor->findChild<VimMode *>() : nullptr;
    modeLabel->setText(vim && vim->isEnabled() ? vim->modeName() : tr("EDIT"));
}

void BitPadFeatures::showCommandPalette()
{
    QDialog dialog(window);
    dialog.setWindowTitle(tr("Command palette"));
    dialog.resize(620, 430);
    auto *layout = new QVBoxLayout(&dialog);
    auto *filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(tr("Type a command, theme, or menu action…"));
    auto *list = new QListWidget(&dialog);
    layout->addWidget(filter);
    layout->addWidget(list);
    QHash<QListWidgetItem *, QPointer<QAction>> commands;
    for (auto *action : window->findChildren<QAction *>()) {
        if (action->isSeparator() || !action->isEnabled() || !action->isVisible() || action->menu()) continue;
        QString label = action->text();
        label.remove('&');
        if (label.isEmpty()) continue;
        if (!action->shortcut().isEmpty()) label += "    " + action->shortcut().toString(QKeySequence::NativeText);
        auto *item = new QListWidgetItem(label, list);
        commands.insert(item, action);
    }
    list->sortItems();
    if (list->count()) list->setCurrentRow(0);
    connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString &query) {
        QListWidgetItem *first = nullptr;
        for (int i=0; i<list->count(); ++i) {
            auto *item=list->item(i);
            item->setHidden(!item->text().contains(query, Qt::CaseInsensitive));
            if (!item->isHidden() && !first) first=item;
        }
        if (first) list->setCurrentItem(first);
    });
    connect(filter, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    filter->setFocus();
    if (dialog.exec() == QDialog::Accepted) {
        auto *item = list->currentItem();
        if (item && !item->isHidden() && commands.value(item)) commands.value(item)->trigger();
    }
}
