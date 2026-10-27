// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecoveryTest.h"
#include "ApplicationSettings.h"
#include "BookMarkDecorator.h"
#include "EditorManager.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "SessionManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonObject>
#include <QPointer>
#include <QTemporaryDir>

namespace {
QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray contents(ScintillaNext *editor)
{
    return QByteArray(reinterpret_cast<const char *>(editor->characterPointer()), editor->length());
}
struct ProfileOverride {
    QByteArray previous = qgetenv("BIT8PAD_PROFILE");
    bool existed = qEnvironmentVariableIsSet("BIT8PAD_PROFILE");
    explicit ProfileOverride(const QString &path) { qputenv("BIT8PAD_PROFILE", path.toUtf8()); }
    ~ProfileOverride() { if (existed) qputenv("BIT8PAD_PROFILE", previous); else qunsetenv("BIT8PAD_PROFILE"); }
};
}

QJsonArray runRecoveryChecks(NotepadNextApplication *app, MainWindow *window)
{
    QJsonArray checks;
    auto check = [&](const QString &name, bool passed) {
        checks.append(QJsonObject{{"name", "recovery-" + name}, {"passed", passed}});
    };
    QTemporaryDir profile;
    check("isolated-profile", profile.isValid());
    if (!profile.isValid()) return checks;
    ProfileOverride profileOverride(profile.path());
    const auto initialEditors = window->editors();
    QPointer<ScintillaNext> originalCurrent = window->currentEditor();
    const QString keptPath = profile.filePath("kept-original.txt");
    const QString deletedPath = profile.filePath("deleted-original.txt");
    const QByteArray diskBytes("unchanged original\r\n");
    const QByteArray unsaved = QString::fromUtf8("T\u00fcrk\u00e7e \u0131\u0130\u011f\u011e\u015f\u015e \U0001f30d\r\nsecond line\n").toUtf8();
    const QByteArray temporaryText = QString::fromUtf8("# new recovery \U0001f30d\nprint('T\u00fcrkiye')\n").toUtf8();
    check("fixture-files", write(keptPath, diskBytes) && write(deletedPath, diskBytes));
    auto *kept = ScintillaNext::fromFile(keptPath);
    auto *deleted = ScintillaNext::fromFile(deletedPath);
    if (!kept || !deleted) {
        check("fixture-editors", false);
        delete kept;
        delete deleted;
        return checks;
    }
    for (auto *editor : {kept, deleted}) {
        app->getEditorManager()->manageEditor(editor);
        editor->setText(unsaved.constData());
        editor->setEncoding("windows-1254");
        app->setEditorLanguage(editor, "C++");
        editor->gotoPos(8);
        if (auto *bookmarks = editor->findChild<BookMarkDecorator *>()) bookmarks->addBookmark(1);
    }
    auto *temporary = app->getEditorManager()->createEditor("recovery-new-fixture");
    temporary->setText(temporaryText.constData());
    temporary->setEncoding("UTF-16BE");
    app->setEditorLanguage(temporary, "Python");
    window->switchToEditor(deleted);
    SessionManager session(app, SessionManager::SavedFile | SessionManager::UnsavedFile | SessionManager::TempFile);
    const bool saved = session.saveSession(window);
    check("commit-with-unrepresentable-unsaved-text", saved);
    const QString sessionPath = profile.filePath("session");
    const QString manifestPath = QDir(sessionPath).filePath("session.ini");
    const QByteArray manifest = read(manifestPath);
    const QString keptSnapshot = kept->QObject::property("8bitpadSnapshotFile").toString();
    const QString deletedSnapshot = deleted->QObject::property("8bitpadSnapshotFile").toString();
    check("snapshots-preserve-unicode", saved && read(QDir(sessionPath).filePath(keptSnapshot)) == unsaved &&
          read(QDir(sessionPath).filePath(deletedSnapshot)) == unsaved);
    check("manifest-not-empty", !manifest.isEmpty());
    const bool savedAgain = saved && session.saveSession(window);
    check("unchanged-snapshot-reused", savedAgain && kept->QObject::property("8bitpadSnapshotFile").toString() == keptSnapshot);

    // Force a deterministic commit failure on both Windows and Linux while
    // retaining the previous manifest and the snapshots it references.
    const QString heldManifest = profile.filePath("retained-session.ini");
    const bool blocked = savedAgain && QFile::rename(manifestPath, heldManifest) && QDir().mkdir(manifestPath);
    check("failure-fixture", blocked);
    if (blocked) {
        kept->appendText(7, "changed");
        const bool refused = !session.saveSession(window);
        check("failed-commit-retains-prior-session", refused && read(heldManifest) == manifest &&
              read(QDir(sessionPath).filePath(keptSnapshot)) == unsaved && read(QDir(sessionPath).filePath(deletedSnapshot)) == unsaved);
        QDir().rmdir(manifestPath);
        check("restore-manifest-fixture", QFile::rename(heldManifest, manifestPath));
    }
    kept->close();
    deleted->close();
    temporary->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check("deleted-original-fixture", QFile::remove(deletedPath));
    session.loadSession(window);
    ScintillaNext *restoredKept = nullptr;
    ScintillaNext *restoredDeleted = nullptr;
    ScintillaNext *restoredTemporary = nullptr;
    for (auto *editor : window->editors()) {
        if (initialEditors.contains(editor)) continue;
        if (editor->getName() == "kept-original.txt") restoredKept = editor;
        else if (editor->getName() == "deleted-original.txt") restoredDeleted = editor;
        else if (editor->getName() == "recovery-new-fixture") restoredTemporary = editor;
    }
    check("existing-original-unsaved-text", restoredKept && restoredKept->isFile() && contents(restoredKept) == unsaved && restoredKept->canSaveToDisk());
    check("deleted-original-unsaved-text", restoredDeleted && !restoredDeleted->isFile() && contents(restoredDeleted) == unsaved && restoredDeleted->canSaveToDisk());
    check("selected-legacy-encoding", restoredKept && restoredDeleted &&
          restoredKept->encodingName().compare("windows-1254", Qt::CaseInsensitive) == 0 &&
          restoredDeleted->encodingName().compare("windows-1254", Qt::CaseInsensitive) == 0);
    check("recovered-language", restoredKept && restoredDeleted && restoredKept->languageName == "C++" && restoredDeleted->languageName == "C++");
    check("recovered-caret", restoredKept && restoredKept->currentPos() == 8);
    auto *bookmarks = restoredKept ? restoredKept->findChild<BookMarkDecorator *>() : nullptr;
    check("recovered-bookmark", bookmarks && bookmarks->isBookmarkSet(1));
    check("new-document-text-and-encoding", restoredTemporary && !restoredTemporary->isFile() && contents(restoredTemporary) == temporaryText &&
          restoredTemporary->encodingName() == "UTF-16BE" && restoredTemporary->languageName == "Python");
    check("original-disk-bytes-untouched", read(keptPath) == diskBytes && !QFile::exists(deletedPath));
    check("current-editor-restored", restoredDeleted && window->currentEditor() == restoredDeleted);
    // Remove only documents created by this isolated helper, including restored
    // copies of pre-existing temporary smoke-test documents.
    for (auto *editor : window->editors())
        if (!initialEditors.contains(editor)) editor->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (originalCurrent) window->switchToEditor(originalCurrent);
    return checks;
}
