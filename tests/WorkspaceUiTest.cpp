// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorkspaceUiTest.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "EditorManager.h"
#include "ScintillaNext.h"
#include "ThemeManager.h"
#include "LanguageMenu.h"
#include "MarkdownPreview.h"
#include "FolderAsWorkspaceDock.h"
#include "PreferencesDialog.h"
#include "TerminalPreferences.h"
#include "FileAssociationsDialog.h"
#include "Readability.h"
#include "DockManager.h"
#include "DockWidgetTab.h"
#include <QFontDatabase>
#include <QFontComboBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QPointer>
#include <QProcess>
#include <QLocalServer>
#include <QUuid>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextFrame>
#include <QTimer>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeWidget>
#include <QPushButton>
#include <functional>

namespace {
void settle() { QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents); }
bool waitFor(const std::function<bool()> &ready, int timeout = 3000)
{
    QElapsedTimer clock; clock.start();
    while (!ready() && clock.elapsed() < timeout) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ready();
}
}

QJsonArray runWorkspaceUiChecks(NotepadNextApplication *app, MainWindow *window, const QString &output)
{
    QJsonArray checks;
    const auto check = [&](const QString &name, bool ok) { checks.append(QJsonObject{{"name", name}, {"passed", ok}}); };
    const QPointer<ScintillaNext> original = window->currentEditor();
    const QString oldTheme = app->getThemeManager()->currentThemeId();
    const QString originalLanguage = original ? original->languageName : QString();
    const QString originalFont = app->getSettings()->fontName();
    for (const auto &candidate : {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono")}) {
        if (QFontDatabase::families().contains(candidate)) { app->getSettings()->setFontName(candidate); break; }
    }
    auto *language = window->findChild<LanguageMenu *>();
    check("workspace-language-helper", language != nullptr);
    if (language) {
        const bool oldFavorite = language->isFavorite("Python");
        language->setFavorite("Python", true);
        language->actionForLanguage("Python")->trigger();
        check("workspace-language-action-selects-editor", window->currentEditor()->languageName == "Python");
        auto *favorites = window->findChild<QMenu *>("languageFavorites");
        check("workspace-favorite-action-shared", favorites && favorites->actions().contains(language->actionForLanguage("Python")));
        language->setFavorite("Python", oldFavorite);
    }
    QTemporaryDir fixture(QDir(output).filePath("workspace-XXXXXX"));
    const QString fixtureRoot = fixture.path();
    check("workspace-fixture", fixture.isValid());
    if (!fixture.isValid()) return checks;
    QDir().mkpath(QDir(fixtureRoot).filePath("docs"));
    QDir().mkpath(QDir(fixtureRoot).filePath(".github/workflows"));
    const QString readme = QDir(fixtureRoot).filePath("docs/README.md");
    window->newFile();
    ScintillaNext *markdown = window->currentEditor();
    const QByteArray source = "# 8bitPad\n\nA compact native editor for Windows and Linux.\n\n## Workspace\n\n- [x] File creation with your extension\n- [x] Favorite languages\n\n| Feature | Status |\n| --- | --- |\n| Markdown preview | Ready |\n| DevOps profiles | Ready |\n\n```python\n# Preview follows the selected theme\nclass Document:\n    def render(self):\n        return \"Ready\", 42\n```\n\n> Source changes stay in the editor.\n";
    markdown->setText(source.constData());
    check("workspace-markdown-save", window->saveFileAs(markdown, readme));
    auto *preview = window->findChild<MarkdownPreview *>();
    auto *previewAction = window->findChild<QAction *>("actionMarkdownPreview");
    auto *folder = window->findChild<FolderAsWorkspaceDock *>();
    check("workspace-preview-actions", preview && previewAction && folder);
    if (!preview || !previewAction || !folder) return checks;
    const QString oldRoot = folder->rootPath();
#ifdef Q_OS_WIN
    QLocalServer probe;
    probe.setSocketOptions(QLocalServer::UserAccessOption);
    const bool pipeAllowed = probe.listen("8bitPad-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!pipeAllowed && (probe.serverError() == QAbstractSocket::SocketAccessError
        || probe.errorString().contains(QStringLiteral("Access is denied"), Qt::CaseInsensitive))) {
        checks.append(QJsonObject{{"name", "command-line-live-handoff"}, {"passed", true}, {"skipped", true},
            {"reason", "This test host denies user-local named pipes; run the live handoff check from normal PowerShell."}});
    } else {
        check("command-line-pipe-capability", pipeAllowed);
    // This smoke instance has a distinct identity from the user's regular Pad.
    const QString cliRoot = QDir(fixtureRoot).filePath(QString::fromUtf8("cli \xC3\xBC & % space"));
    const QString cliName = QString::fromUtf8("some \xC3\xBC & %.txt");
    const QString cliFile = QDir(cliRoot).filePath(cliName);
    check("command-line-fixture-directory", QDir().mkpath(cliRoot));
    QFile cliInput(cliFile);
    const bool cliWritten = cliInput.open(QIODevice::WriteOnly) && cliInput.write("command-line fixture\n") > 0;
    cliInput.close();
    check("command-line-fixture-file", cliWritten);
    QProcess command;
    command.setWorkingDirectory(cliRoot);
    command.start(QDir(QCoreApplication::applicationDirPath()).filePath("8p.exe"),
        {"--smoke-test", QDir(output).absolutePath(), ".", cliName});
    const bool commandStarted = command.waitForStarted(3000);
    check("command-line-native-alias-starts", commandStarted);
    const bool cliOpened = waitFor([&] {
        return QDir(folder->rootPath()).absolutePath() == QDir(cliRoot).absolutePath()
            && app->getEditorManager()->getEditorByFilePath(cliFile);
    }, 10000);
    const bool cliReturned = waitFor([&] { return command.state() == QProcess::NotRunning; }, 6000);
    check("command-line-native-alias-returns", cliReturned && command.exitStatus() == QProcess::NormalExit && command.exitCode() == 0);
    check("command-line-relative-folder-and-file", cliOpened);
    if (!cliOpened) qWarning() << "Command forwarding failed:" << command.readAllStandardOutput()
        << command.readAllStandardError() << "exit:" << command.exitCode() << "state:" << command.state()
        << "error:" << command.errorString() << "workspace:" << folder->rootPath();
    if (auto *opened = app->getEditorManager()->getEditorByFilePath(cliFile))
        window->closeFile(opened, true);
    }
#endif
    folder->setRootPath(fixtureRoot, true);
    // This is the same signal emitted by the Explorer's context action.
    emit folder->markdownPreviewRequested(readme);
    check("workspace-explorer-opens-preview", waitFor([&] { return preview->isVisible() && preview->renderedDocument()->toPlainText().contains("Workspace"); }));
    const QByteArray live = "\nLive unsaved preview\n";
    markdown->appendText(live.size(), live.constData());
    check("workspace-preview-unsaved-live-update", waitFor([&] { return preview->renderedDocument()->toPlainText().contains("Live unsaved preview"); }));
    QFile disk(readme);
    check("workspace-preview-does-not-save", disk.open(QIODevice::ReadOnly) && disk.readAll() == source);
    disk.close();
    const QString moved = QDir(fixtureRoot).filePath("guide");
    check("workspace-directory-move", QDir().rename(QDir(fixtureRoot).filePath("docs"), moved));
    emit folder->pathRenamed(QDir(fixtureRoot).filePath("docs"), moved);
    const QString movedReadme = QDir(moved).filePath("README.md");
    check("workspace-directory-move-updates-dirty-tab", markdown->getFileInfo().absoluteFilePath() == movedReadme &&
          !markdown->isSavedToDisk() && markdown->canUndo() && markdown->get_text_range(0, markdown->length()).contains(live));
    check("workspace-directory-move-updates-preview", waitFor([&] { return preview->sourcePath() == movedReadme; }));

    // Bound the old feedback loop so this regression reports a failure instead
    // of overflowing the test process's stack on platforms that normalize fonts.
    app->getSettings()->setValue("appearance/bonusThemesUnlocked", false);
    const QString themeBeforeUnlock = app->getThemeManager()->currentThemeId();
    // Emulate an older profile with a saved bonus theme but no unlock flag.
    app->getThemeManager()->setTheme("cyberpunk-silverhand");
    const QString preferencesFont = app->getSettings()->fontName();
    int fontNotifications = 0;
    QPointer<QFontComboBox> fontPicker;
    const auto fontGuard = QObject::connect(app->getSettings(), &ApplicationSettings::fontNameChanged, window,
        [&](const QString &) {
            if (++fontNotifications > 8 && fontPicker) fontPicker->blockSignals(true);
        });
    PreferencesDialog preferences(app->getSettings(), window);
    fontPicker = preferences.findChild<QFontComboBox *>("fcbDefaultFont");
    check("workspace-preferences-font-picker", fontPicker != nullptr);
    if (fontPicker) {
        int reflectedChanges = 0;
        const auto fontFeedback = QObject::connect(fontPicker, &QFontComboBox::currentFontChanged, window,
            [&](const QFont &) { ++reflectedChanges; });
        const QString unavailableFont = QStringLiteral("8bitPad Regression Font Not Installed");
        fontNotifications = 0;
        app->getSettings()->setFontName(unavailableFont);
        check("workspace-preferences-font-feedback-is-bounded", fontNotifications == 1 && !fontPicker->signalsBlocked());
        check("workspace-preferences-retains-unavailable-font", app->getSettings()->fontName() == unavailableFont);
        check("workspace-preferences-font-refresh-does-not-write-back", reflectedChanges == 0);
        if (fontPicker->count() > 1) {
            fontNotifications = 0;
            fontPicker->setCurrentIndex(fontPicker->currentIndex() == 0 ? 1 : 0);
            check("workspace-preferences-user-font-selection", fontNotifications == 1 &&
                app->getSettings()->fontName() == fontPicker->currentFont().family());
        }
        QObject::disconnect(fontFeedback);
        app->getSettings()->setFontName(preferencesFont);
    }
    QObject::disconnect(fontGuard);
    auto *categories = preferences.findChild<QListWidget *>("preferencesCategories");
    auto *pages = preferences.findChild<QStackedWidget *>("preferencesPages");
    auto *themes = preferences.findChild<QComboBox *>("comboBoxTheme");
    check("workspace-preferences-categories", categories && pages && categories->count() == 5 && pages->count() == 5);
    check("workspace-preferences-theme-catalog", themes && themes->count() == app->getThemeManager()->availableThemes().size());
    auto *versionButton = preferences.findChild<QPushButton *>("applicationVersionButton");
    check("workspace-version-visible", versionButton && versionButton->text().contains(QCoreApplication::applicationVersion()));
    const auto checkThemeMenu = [&](bool unlocked) {
        for (const auto &theme : ThemeManager::catalog()) {
            auto *action = window->findChild<QAction *>("theme_" + theme.id);
            const bool expected = unlocked || !ThemeManager::isBonusTheme(theme.id);
            check("workspace-theme-menu-" + QString(unlocked ? "unlocked-" : "locked-") + theme.id,
                action && action->isVisible() == expected && action->isEnabled() == expected);
        }
    };
    const auto checkCommandPalette = [&](bool unlocked) {
        auto *action = window->findChild<QAction *>("actionCommandPalette");
        check("workspace-command-palette-action", action != nullptr);
        if (!action) return;
        bool inspected = false;
        QTimer::singleShot(0, window, [&] {
            auto *dialog = window->findChild<QDialog *>("commandPaletteDialog");
            auto *commands = dialog ? dialog->findChild<QListWidget *>("commandPaletteCommands") : nullptr;
            if (commands) {
                inspected = true;
                for (const auto &theme : ThemeManager::catalog()) {
                    if (!ThemeManager::isBonusTheme(theme.id)) continue;
                    check("workspace-command-palette-" + QString(unlocked ? "unlocked-" : "locked-") + theme.id,
                        !commands->findItems(theme.name, Qt::MatchExactly).isEmpty() == unlocked);
                }
            }
            if (dialog) dialog->reject();
            else if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) modal->reject();
        });
        action->trigger();
        check("workspace-command-palette-inspected", inspected);
    };
    checkThemeMenu(false);
    checkCommandPalette(false);
    check("workspace-locked-saved-theme-not-offered", themes && themes->currentIndex() == -1 &&
        themes->findData("cyberpunk-silverhand") == -1 &&
        app->getThemeManager()->currentThemeId() == "cyberpunk-silverhand");
    if (versionButton && themes) {
        const QString selectedTheme = app->getThemeManager()->currentThemeId();
        for (int tap = 0; tap < 6; ++tap) versionButton->click();
        check("workspace-bonus-hidden-before-seven", themes->findData("cyberpunk-silverhand") < 0 &&
            themes->findData("cyberpunk-v") < 0 && themes->findData("cyberpunk-k") < 0);
        versionButton->click();
        check("workspace-bonus-themes-unlocked", app->getThemeManager()->bonusThemesUnlocked() &&
            themes->findData("blueberry") >= 0 && themes->findData("blueberry-light") >= 0 &&
            themes->findData("cyberpunk-silverhand") >= 0 && themes->findData("cyberpunk-v") >= 0 && themes->findData("cyberpunk-k") >= 0);
        check("workspace-bonus-keeps-current-theme", app->getThemeManager()->currentThemeId() == selectedTheme);
        checkThemeMenu(true);
        checkCommandPalette(true);
        check("workspace-unlock-restores-saved-selection", themes->currentData().toString() == selectedTheme);
    }
    app->getThemeManager()->setTheme(themeBeforeUnlock);
    check("workspace-terminal-discovery-is-lazy", !preferences.findChild<TerminalPreferences *>());
    emit folder->terminalSettingsRequested();
    check("workspace-explorer-opens-terminal-settings", waitFor([&] {
        return pages && pages->currentIndex() == 4 && preferences.isVisible();
    }));
    auto *terminalPreferences = preferences.findChild<TerminalPreferences *>();
    check("workspace-terminal-settings-controls", terminalPreferences &&
        terminalPreferences->findChild<QComboBox *>("terminalDefaultCombo") &&
        terminalPreferences->findChild<QWidget *>("terminalCustomArguments") &&
        terminalPreferences->findChild<QWidget *>("terminalRefreshButton"));
    preferences.hide();
    if (categories && pages) {
        categories->setCurrentRow(1);
        check("workspace-preferences-navigation", pages->currentIndex() == 1);
    }
    const int oldFontSize = app->getSettings()->fontSize();
    if (auto *size = preferences.findChild<QSpinBox *>("spbDefaultFontSize")) {
        size->setValue(oldFontSize == 12 ? 13 : 12);
        check("workspace-preferences-live-binding", app->getSettings()->fontSize() == size->value());
        app->getSettings()->setFontSize(oldFontSize);
        check("workspace-preferences-setting-feedback", size->value() == oldFontSize);
    } else check("workspace-preferences-font-control", false);
    if (auto *completion = preferences.findChild<QCheckBox *>("checkBoxAutoCompletion")) {
        const bool wasEnabled = app->getSettings()->autoCompletion();
        completion->setChecked(!wasEnabled);
        check("workspace-preferences-completion-binding", app->getSettings()->autoCompletion() == !wasEnabled);
        completion->setChecked(wasEnabled);
    } else check("workspace-preferences-completion-control", false);

    int associationApplyCalls = 0;
    QStringList appliedExtensions;
    FileAssociationsDialog::Callbacks associationCallbacks;
    associationCallbacks.catalog = FileAssociations::catalog;
    associationCallbacks.registeredExtensions = [] { return QStringList(); };
    associationCallbacks.parseExtensions = FileAssociations::parseExtensions;
    associationCallbacks.contextMenuInstalled = [] { return false; };
    associationCallbacks.setContextMenuEnabled = [&](bool) {
        check("workspace-associations-unexpected-context-mutation", false);
        return FileAssociations::BatchResult();
    };
    associationCallbacks.apply = [&](const QStringList &extensions, bool) {
        ++associationApplyCalls;
        appliedExtensions = extensions;
        FileAssociations::BatchResult result;
        result.registeredExtensions = extensions;
        return result;
    };
    FileAssociationsDialog associations(window, associationCallbacks);
    auto *associationTree = associations.findChild<QTreeWidget *>("associationTree");
    auto *associationAll = associations.findChild<QPushButton *>("associationSelectAll");
    auto *associationApply = associations.findChild<QPushButton *>("associationApply");
    check("workspace-associations-menu-action", window->findChild<QAction *>("actionFileAssociations") != nullptr);
    check("workspace-associations-selection-starts-empty", associations.selectedExtensions().isEmpty() && associationApplyCalls == 0);
    if (associationTree) {
        for (int group = 0; group < associationTree->topLevelItemCount(); ++group) {
            auto *item = associationTree->topLevelItem(group);
            for (int child = 0; child < item->childCount(); ++child) {
                auto *extension = item->child(child);
                if (QStringList{"txt", "log", "md", "json", "yaml", "py", "cpp"}.contains(extension->data(0, Qt::UserRole).toString()))
                    extension->setCheckState(0, Qt::Checked);
            }
            if (group > 0) item->setExpanded(false);
        }
    }
    check("workspace-associations-multiple-selection", associations.selectedExtensions().size() >= 3 && associationApplyCalls == 0);
    window->resize(1400, 880);
    for (auto *dock : window->findChildren<QDockWidget *>())
        if (dock != preview && dock != folder) dock->hide();
    window->resizeDocks({preview}, {560}, Qt::Horizontal);
    for (const auto &theme : ThemeManager::catalog()) {
        app->getThemeManager()->setTheme(theme.id);
        settle();
        preview->refreshTheme();
        preview->renderNow();
        const QString prefix = "workspace-theme-" + theme.id + '-';
        const QPalette palette = QApplication::palette();
        check(prefix + "control-contrast", contrastRatio(palette.color(QPalette::Text), palette.color(QPalette::Base)) >= 4.49 &&
            contrastRatio(palette.color(QPalette::ButtonText), palette.color(QPalette::Button)) >= 4.49 &&
            contrastRatio(palette.color(QPalette::HighlightedText), palette.color(QPalette::Highlight)) >= 4.49);
        auto *dockManager = window->findChild<ads::CDockManager *>();
        check(prefix + "tabs-use-application-theme", dockManager && dockManager->styleSheet().isEmpty());
        bool activeUnderline = false;
        bool activeAccentFill = false;
        bool activeAccentText = false;
        bool activeIconBacking = false;
        for (auto *tab : window->findChildren<ads::CDockWidgetTab *>()) {
            if (!tab->isVisible() || !tab->isActiveTab()) continue;
            // Inspect the composited window, since a correct standalone tab can
            // still have its underline clipped by the scroll-area viewport.
            const QPixmap windowImage = window->grab();
            const qreal scale = windowImage.devicePixelRatio();
            const QPoint origin = tab->mapTo(window, QPoint());
            const QImage rendered = windowImage.toImage().copy(QRect(
                qRound(origin.x()*scale), qRound(origin.y()*scale),
                qRound(tab->width()*scale), qRound(tab->height()*scale)));
            if (theme.id.startsWith("cyberpunk-")) {
                int accentPixels = 0;
                for (int y = 0; y < rendered.height(); ++y)
                    for (int x = 0; x < rendered.width(); ++x)
                        if (rendered.pixelColor(x, y).rgb() == theme.accent.rgb()) ++accentPixels;
                activeAccentFill |= accentPixels > rendered.width() * rendered.height() / 2;
                for (auto *label : tab->findChildren<QLabel *>()) {
                    if (label->isVisible() && !label->text().isEmpty())
                        activeAccentText |= contrastRatio(label->palette().color(QPalette::WindowText), theme.accent) >= 4.5;
                    if (label->isVisible() && !label->pixmap().isNull())
                        activeIconBacking |= label->palette().color(QPalette::Window) == theme.background;
                }
            }
            for (int y = qMax(0, rendered.height() - 5); y < rendered.height(); ++y)
                for (int x = 2; x < rendered.width() - 2; ++x)
                    if (rendered.pixelColor(x,y).rgb() == theme.accent.rgb()) activeUnderline = true;
        }
        check(prefix + "active-tab-underline", activeUnderline);
        if (theme.id.startsWith("cyberpunk-")) {
            check(prefix + "active-tab-accent-fill", activeAccentFill);
            check(prefix + "active-tab-accent-readable", activeAccentText);
            check(prefix + "active-tab-icon-dark-backing", activeIconBacking);
            check(prefix + "preferences-selection-readable", categories &&
                contrastRatio(categories->palette().color(QPalette::HighlightedText),
                    categories->palette().color(QPalette::Highlight)) >= 4.5);
            const QImage header = window->menuBar()->grab().toImage();
            int primaryPixels = 0, secondaryPixels = 0;
            for (int y = 0; y < header.height(); ++y)
                for (int x = 0; x < header.width(); ++x) {
                    const auto pixel = header.pixelColor(x, y).rgb();
                    if (pixel == theme.accent.rgb()) ++primaryPixels;
                    if (pixel == theme.secondaryAccent.rgb()) ++secondaryPixels;
                }
            check(prefix + "header-primary-accent", primaryPixels > header.width() * header.height() / 2);
            check(prefix + "header-secondary-accent", secondaryPixels >= header.width());
        }
        check(prefix + "preferences-sync", themes && themes->currentData().toString() == theme.id);
        check(prefix + "association-control-contrast", contrastRatio(associations.palette().color(QPalette::Text),
              associations.palette().color(QPalette::Base)) >= 4.49);
        check(prefix + "terminal-settings-contrast", terminalPreferences &&
            contrastRatio(terminalPreferences->palette().color(QPalette::Text),
                          terminalPreferences->palette().color(QPalette::Base)) >= 4.49);
        auto *document = preview->renderedDocument();
        check(prefix + "preview-background", document->rootFrame()->frameFormat().background().color() == theme.background);
        bool readable = true;
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
            const QBrush background = block.blockFormat().background();
            const QColor paper = background.style() == Qt::NoBrush ? theme.background : background.color();
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (fragment.isValid() && !fragment.text().trimmed().isEmpty()) {
                    const QBrush foreground = fragment.charFormat().foreground();
                    if (foreground.style() != Qt::NoBrush && contrastRatio(foreground.color(), paper) < 4.49) readable = false;
                }
            }
        }
        check(prefix + "preview-text-contrast", readable);
        const auto *save = window->findChild<QAction *>("actionSave");
        check(prefix + "toolbar-icon", save && !save->icon().pixmap(32,32).isNull());
        if (theme.id == "classic-light" || theme.id == "ayu-dark" || theme.id == "github-light" || theme.id.startsWith("cyberpunk-")) {
            check(prefix + "screenshot", window->grab().save(QDir(output).filePath("workspace-" + theme.id + ".png")));
            if (theme.id.startsWith("cyberpunk-") && original) {
                const QString screenshotLanguage = original->languageName;
                app->setEditorLanguage(original, "C++");
                window->switchToEditor(original);
                preview->hide();
                settle();
                check(prefix + "code-screenshot", window->grab().save(QDir(output).filePath("editor-" + theme.id + ".png")));
                app->setEditorLanguage(original, screenshotLanguage);
                window->switchToEditor(markdown);
                preview->show();
                settle();
            }
            preferences.show();
            settle();
            check(prefix + "preferences-screenshot", preferences.grab().save(QDir(output).filePath("preferences-" + theme.id + ".png")));
            preferences.showTerminalSettings();
            settle();
            check(prefix + "terminal-settings-screenshot", preferences.grab().save(QDir(output).filePath("terminal-" + theme.id + ".png")));
            if (categories) categories->setCurrentRow(1);
            preferences.hide();
            associations.show();
            settle();
            check(prefix + "association-screenshot", associations.grab().save(QDir(output).filePath("associations-" + theme.id + ".png")));
            associations.hide();
        }
    }
    if (associationAll && associationApply) {
        associationAll->click();
        int catalogCount = 0;
        for (const auto &group : FileAssociations::catalog()) catalogCount += group.extensions.size();
        check("workspace-associations-select-all", associations.selectedExtensions().size() == catalogCount);
        const auto selected = associations.selectedExtensions();
        associationApply->click();
        check("workspace-associations-applies-one-batch", associationApplyCalls == 1 && appliedExtensions == selected);
    } else check("workspace-associations-bulk-controls", false);
    if (original) {
        app->getThemeManager()->setTheme("classic-light");
        preview->hide();
        window->switchToEditor(original);
        app->setEditorLanguage(original, originalLanguage);
        settle();
        check("workspace-classic-editor-screenshot", window->grab().save(QDir(output).filePath("editor-classic-light.png")));
    }
    app->getThemeManager()->setTheme(oldTheme);
    app->getSettings()->setFontName(originalFont);
    preferences.hide();
    preview->hide();
    window->newFile();
    auto *workflow = window->currentEditor();
    workflow->setText("name: CI\non: [push]\njobs:\n  build:\n    runs-on: ubuntu-latest\n    steps:\n      - run: echo ready\n");
    check("workspace-workflow-save", window->saveFileAs(workflow, QDir(fixtureRoot).filePath(".github/workflows/check.yml")));
    check("workspace-workflow-auto-profile", workflow->languageName == "YAML (GitHub Actions)");
    check("workspace-workflow-completion", app->getLanguageKeywords(workflow->languageName).contains("runs-on"));
    check("workspace-devops-assistance-action", window->findChild<QAction *>("actionDevOpsAssistance") != nullptr);
    window->closeFile(workflow, true);
    window->closeFile(markdown, true);
    if (oldRoot.isEmpty()) folder->closeFolder(); else folder->setRootPath(oldRoot, false);
    if (original) { window->switchToEditor(original); app->setEditorLanguage(original, originalLanguage); }
    return checks;
}
