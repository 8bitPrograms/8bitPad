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


#include "BookMarkDecorator.h"
#include "ScintillaNext.h"
#include "MainWindow.h"
#include "SessionManager.h"
#include "EditorManager.h"
#include "NotepadNextApplication.h"

#include <QDir>
#include <QStandardPaths>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <memory>


static QString RandomSessionFileName()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// QList<int> cannot be automatically serialized to/from QSettings (i.e. QVariant) so turn it to a QVariantList
static QVariantList QListToQVariantList(const QList<int> intList)
{
    QVariantList vl;
    for (const int i : intList){
        vl.append(i);
    }
    return vl;
}

// Do the opposite of the function above
static QList<int> QVariantListToQList(const QVariantList &variantList) {
    QList<int> intList;
    for (const QVariant &variant : variantList) {
        intList.append(variant.toInt());
    }
    return intList;
}

SessionManager::SessionManager(NotepadNextApplication *app, SessionFileTypes types)
    : app(app)
{
    setSessionFileTypes(types);
}

void SessionManager::setSessionFileTypes(SessionFileTypes types)
{
    fileTypes = types;
}

QDir SessionManager::sessionDirectory() const
{
    QDir d(ApplicationSettings::profileDirectory() + QStringLiteral("/session"));
    QDir().mkpath(d.absolutePath());
    return d;
}

bool SessionManager::saveIntoSessionDirectory(ScintillaNext *editor, const QString &sessionFileName) const
{
    if (editor->QObject::property("8bitpadSnapshotFile").toString() == sessionFileName &&
        editor->QObject::property("8bitpadSnapshotRevision") == editor->QObject::property("8bitpadContentRevision") &&
        QFileInfo::exists(sessionDirectory().filePath(sessionFileName)))
        return true;
    if (editor->saveRecoveryCopyAs(sessionDirectory().filePath(sessionFileName)) != QFileDevice::NoError)
        return false;
    editor->QObject::setProperty("8bitpadSnapshotFile", sessionFileName);
    editor->QObject::setProperty("8bitpadSnapshotRevision", editor->QObject::property("8bitpadContentRevision"));
    return true;
}

QString SessionManager::snapshotFileName(ScintillaNext *editor) const
{
    const QString previous = editor->QObject::property("8bitpadSnapshotFile").toString();
    if (!previous.isEmpty() && editor->QObject::property("8bitpadSnapshotRevision") == editor->QObject::property("8bitpadContentRevision") &&
        QFileInfo::exists(sessionDirectory().filePath(previous)))
        return previous;
    return RandomSessionFileName();
}

SessionManager::SessionFileType SessionManager::determineType(ScintillaNext *editor) const
{
    if (editor->isFile()) {
        if (editor->isSavedToDisk()) {
            return SessionManager::SavedFile;
        }
        else {
            return SessionManager::UnsavedFile;
        }
    }
    else {
        if (!editor->isSavedToDisk()) {
            return SessionManager::TempFile;
        }
        else {
            return SessionManager::None;
        }
    }
}

void SessionManager::clear() const
{
    clearSettings();
    clearDirectory();
}

void SessionManager::clearSettings() const
{
    ApplicationSettings settings;;

    // Clear everything out. There can be left over entries that are no longer needed
    settings.beginGroup("CurrentSession");
    settings.remove("");
}

void SessionManager::clearDirectory() const
{
    QDir d = sessionDirectory();

    for (const QString &f : d.entryList()) {
        d.remove(f);
    }
}

bool SessionManager::saveSession(MainWindow *window)
{
    qInfo(Q_FUNC_INFO);

    QDir directory = sessionDirectory();
    // QTemporaryFile retains its native handle on Windows after close(); that
    // prevents QSettings from atomically replacing it. Stage in an owned
    // directory so QSettings can create and replace its own file normally.
    QTemporaryDir staging(directory.filePath(QStringLiteral("manifest-XXXXXX")));
    if (!staging.isValid()) {
        qWarning("Cannot create session manifest; previous session retained");
        return false;
    }
    const QString stagingPath = staging.filePath(QStringLiteral("session.ini"));

    const ScintillaNext *currentEditor = window->currentEditor();
    int currentEditorIndex = 0;
    QSettings settings(stagingPath, QSettings::IniFormat);
    QStringList snapshotFiles;

    settings.beginGroup("CurrentSession");

    settings.beginWriteArray("OpenedFiles");

    int index = 0;
    for (const auto &editor : window->editors()) {
        SessionFileType editorType = determineType(editor);

        if (fileTypes.testFlag(editorType)) {
            settings.setArrayIndex(index);

            if (editorType == SessionManager::SavedFile) {
                storeFileDetails(editor, settings);
            }
            else if (editorType == SessionManager::UnsavedFile) {
                if (!storeUnsavedFileDetails(editor, settings))
                    return false;
                snapshotFiles.append(settings.value("SessionFileName").toString());
            }
            else if (editorType == SessionManager::TempFile) {
                if (!storeTempFile(editor, settings))
                    return false;
                snapshotFiles.append(settings.value("SessionFileName").toString());
            }
            else {
                qWarning("Unknown SessionFileType %d", editorType);
            }

            if (currentEditor == editor) {
                currentEditorIndex = index;
            }

            ++index;
        }
    }

    settings.endArray();

    settings.setValue("CurrentEditorIndex", currentEditorIndex);

    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qWarning("Cannot flush session manifest; previous session retained");
        return false;
    }

    QFile stagedManifest(stagingPath);
    QSaveFile manifest(directory.filePath(QStringLiteral("session.ini")));
    manifest.setDirectWriteFallback(false);
    if (!stagedManifest.open(QIODevice::ReadOnly) || !manifest.open(QIODevice::WriteOnly))
        return false;
    const QByteArray metadata = stagedManifest.readAll();
    if (stagedManifest.error() != QFileDevice::NoError || manifest.write(metadata) != metadata.size() || !manifest.commit()) {
        qWarning("Cannot commit session manifest; previous session retained");
        return false;
    }

    // Only collect old snapshots after all new content and its manifest are durable.
    for (const QString &name : directory.entryList(QDir::Files)) {
        if (name != QStringLiteral("session.ini") && !snapshotFiles.contains(name))
            directory.remove(name);
    }
    return true;
}

void SessionManager::loadSession(MainWindow *window)
{
    qInfo(Q_FUNC_INFO);

    const QString manifestPath = sessionDirectory().filePath(QStringLiteral("session.ini"));
    std::unique_ptr<QSettings> savedSettings;
    if (QFileInfo::exists(manifestPath))
        savedSettings = std::make_unique<QSettings>(manifestPath, QSettings::IniFormat);
    else
        savedSettings = std::make_unique<ApplicationSettings>();
    QSettings &settings = *savedSettings;

    settings.beginGroup("CurrentSession");

    ScintillaNext *currentEditor = Q_NULLPTR;
    const int currentEditorIndex = settings.value("CurrentEditorIndex").toInt();
    const int size = settings.beginReadArray("OpenedFiles");

    // NOTE: In theory the fileTypes should determine what is loaded, however if the session fileTypes
    // change from the last time it was saved then it means the settings were manually altered outside of the app,
    // which is non-standard behavior, so just load anything in the file

    for (int index = 0; index < size; ++index) {
        settings.setArrayIndex(index);

        ScintillaNext *editor = Q_NULLPTR;

        if (settings.contains("Type")) {
            const QString type = settings.value("Type").toString();

            if (type == QStringLiteral("File")) {
                editor = loadFileDetails(settings);
            }
            else if (type == QStringLiteral("UnsavedFile")) {
                editor = loadUnsavedFileDetails(settings);
            }
            else if (type == QStringLiteral("Temp")) {
                editor = loadTempFile(settings);
            }
            else {
                qDebug("Unknown session entry type: %s", qUtf8Printable(type));
            }

            if (editor) {
                 if (currentEditorIndex == index) {
                    currentEditor = editor;
                }
            }
        }
        else {
            qDebug("Unknown session entry type for index %d", index);
        }
    }

    settings.endArray();

    settings.endGroup();

    if (currentEditor) {
        window->switchToEditor(currentEditor);
    }
}

bool SessionManager::willFileGetStoredInSession(ScintillaNext *editor) const
{
    SessionFileType editorType = determineType(editor);

    // See if the editor type is in the currently supported file types
    return fileTypes.testFlag(editorType);
}

void SessionManager::storeFileDetails(ScintillaNext *editor, QSettings &settings)
{
    settings.setValue("Type", "File");
    settings.setValue("FilePath", editor->getFilePath());
    settings.setValue("Encoding", editor->encodingName());

    storeEditorViewDetails(editor, settings);
}

ScintillaNext* SessionManager::loadFileDetails(QSettings &settings)
{
    qInfo(Q_FUNC_INFO);

    const QString filePath = settings.value("FilePath").toString();

    qDebug("Session file: \"%s\"", qUtf8Printable(filePath));

    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);
    if (editor != Q_NULLPTR) {
        qDebug("  file is already open, ignoring");
        return Q_NULLPTR;
    }

    if (QFileInfo::exists(filePath)) {
        editor = ScintillaNext::fromFile(filePath, false, settings.value("Encoding").toString());

        if (editor == Q_NULLPTR) {
            qWarning("  could not be read from disk, ignoring this file for session loading");
            return Q_NULLPTR;
        }

        app->getEditorManager()->manageEditor(editor);

        loadEditorViewDetails(editor, settings);

        return editor;
    }
    else {
        qDebug("  no longer exists on disk, ignoring");
        return Q_NULLPTR;
    }
}

bool SessionManager::storeUnsavedFileDetails(ScintillaNext *editor, QSettings &settings)
{
    const QString sessionFileName = snapshotFileName(editor);

    settings.setValue("Type", "UnsavedFile");
    settings.setValue("FilePath", editor->getFileInfo().absoluteFilePath());
    settings.setValue("SessionFileName", sessionFileName);
    settings.setValue("Encoding", editor->encodingName());
    settings.setValue("Language", editor->languageName);

    storeEditorViewDetails(editor, settings);

    return saveIntoSessionDirectory(editor, sessionFileName);
}

ScintillaNext *SessionManager::loadUnsavedFileDetails(QSettings &settings)
{
    qInfo(Q_FUNC_INFO);

    const QString filePath = settings.value("FilePath").toString();
    const QString sessionFileName = settings.value("SessionFileName").toString();
    const QString sessionFilePath = sessionDirectory().filePath(sessionFileName);
    const QString languageName = settings.value("Language").toString();

    qDebug("Session file: \"%s\"", qUtf8Printable(filePath));
    qDebug("  temp loc: \"%s\"", qUtf8Printable(sessionFilePath));

    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);
    if (editor != Q_NULLPTR) {
        qDebug("  file is already open, ignoring");
        return Q_NULLPTR;
    }

    if (QFileInfo::exists(sessionFilePath)) {
        ScintillaNext *editor = ScintillaNext::fromFile(sessionFilePath);

        if (editor == Q_NULLPTR) {
            qWarning("  could not be read from disk, ignoring this file for session loading");
            return Q_NULLPTR;
        }

        // Since this editor has different file path info, treat this as a temporary buffer
        if (QFileInfo::exists(filePath))
            editor->setFileInfo(filePath);
        else
            editor->detachFileInfo(QFileInfo(filePath).fileName());
        if (settings.contains("Encoding"))
            editor->setEncoding(settings.value("Encoding").toString());
        editor->setTemporary(true);

        app->getEditorManager()->manageEditor(editor);

        loadEditorViewDetails(editor, settings);
        if (!languageName.isEmpty())
            app->setEditorLanguage(editor, languageName);

        return editor;
    }
    else {
        // What if just filePath exists?
        qDebug("  no longer exists on disk, ignoring this file for session loading");
        return Q_NULLPTR;
    }
}

bool SessionManager::storeTempFile(ScintillaNext *editor, QSettings &settings)
{
    const QString sessionFileName = snapshotFileName(editor);

    settings.setValue("Type", "Temp");
    settings.setValue("FileName", editor->getName());
    settings.setValue("SessionFileName", sessionFileName);
    settings.setValue("Language", editor->languageName);
    settings.setValue("Encoding", editor->encodingName());

    storeEditorViewDetails(editor, settings);

    return saveIntoSessionDirectory(editor, sessionFileName);
}

ScintillaNext *SessionManager::loadTempFile(QSettings &settings)
{
    qInfo(Q_FUNC_INFO);

    const QString fileName = settings.value("FileName").toString();
    const QString sessionFileName = settings.value("SessionFileName").toString();
    const QString languageName = settings.value("Language", QString()).toString();
    const QString fullFilePath = sessionDirectory().filePath(sessionFileName);

    qDebug("Session temp file: \"%s\"", qUtf8Printable(fullFilePath));

    if (QFileInfo::exists(fullFilePath)) {
        ScintillaNext *editor = ScintillaNext::fromFile(fullFilePath, false);

        if (editor == Q_NULLPTR) {
            qWarning("  could not be read from disk, ignoring this file for session loading");
            return Q_NULLPTR;
        }

        editor->detachFileInfo(fileName);
        if (settings.contains("Encoding"))
            editor->setEncoding(settings.value("Encoding").toString());
        editor->setTemporary(true);

        app->getEditorManager()->manageEditor(editor);

        loadEditorViewDetails(editor, settings);

        if (!languageName.isEmpty()) {
            qDebug("Setting session file language to \"%s\"", qUtf8Printable(languageName));
            app->setEditorLanguage(editor, languageName);
        }

        return editor;
    }
    else {
        qDebug("  no longer exists on disk, ignoring");
        return Q_NULLPTR;
    }
}

void SessionManager::storeEditorViewDetails(ScintillaNext *editor, QSettings &settings)
{
    settings.setValue("FirstVisibleLine", static_cast<int>(editor->firstVisibleLine() + 1)); // Keep it 1-based in the settings just for human-readability
    settings.setValue("CurrentPosition", static_cast<int>(editor->currentPos()));

    BookMarkDecorator *decorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);
    if (decorator == Q_NULLPTR) {
        qWarning("Editor has no BookMarkDecorator, unable to store bookmarks");
        return;
    }

    QList<int> bookMarkedLines = decorator->bookMarkedLines();
    if (bookMarkedLines.length() > 0)
        settings.setValue("BookMarks", QListToQVariantList(bookMarkedLines));
}

void SessionManager::loadEditorViewDetails(ScintillaNext *editor, QSettings &settings)
{
    const int firstVisibleLine = settings.value("FirstVisibleLine").toInt() - 1;
    const int currentPosition = settings.value("CurrentPosition").toInt();

    editor->setFirstVisibleLine(firstVisibleLine);
    editor->setEmptySelection(currentPosition);

    if (settings.contains("BookMarks"))
    {
        QList<int> bookMarkedLines = QVariantListToQList(settings.value("BookMarks").toList()); // just using .value<QList<int>>() does not work...possibly a Qt bug?

        BookMarkDecorator *decorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);
        if (decorator != Q_NULLPTR) {
            decorator->setBookMarkedLines(bookMarkedLines);
        }
        else {
            qWarning("Editor has no BookMarkDecorator, unable to restore bookmarks");
        }
    }
}
