// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExplorerFileOperations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <filesystem>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

std::filesystem::path nativePath(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}
}

class ExplorerFileOperationsTest : public QObject
{
    Q_OBJECT
private slots:
    void exactNamesAndExtensions();
    void invalidNames_data();
    void invalidNames();
    void neverOverwrite();
    void renameAndMovePreserveContents();
    void caseOnlyRenamePreservesFileAndFolder();
    void refuseTraversalAndWorkspaceRoot();
    void refuseRecursiveDestination();
    void copyDirectoryPreservesHiddenFiles();
    void cancelledCopyLeavesSourceAndNoPartialDestination();
    void rejectLinkEscapesAndCleanFailedCopy();
};

void ExplorerFileOperationsTest::exactNamesAndExtensions()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Explorer::FileOperations files(temp.path());
    for (const QString &name : {QString("notes.md"), QString("script.py"), QString("Dockerfile"), QString(".gitignore")}) {
        const auto result = files.createFile(temp.path(), name);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(QFileInfo(result.path).fileName(), name);
        QVERIFY(QFileInfo(result.path).isFile());
        QVERIFY(!QFileInfo::exists(result.path + ".txt"));
    }
    const auto folder = files.createDirectory(temp.path(), ".github");
    QVERIFY2(folder.ok, qPrintable(folder.error));
    QVERIFY(QFileInfo(folder.path).isDir());
}

void ExplorerFileOperationsTest::invalidNames_data()
{
    QTest::addColumn<QString>("name");
    const QStringList names = {"", " ", ".", "..", "../escape", "sub/file.py", "sub\\file.py", "C:drive",
                               "NUL", "con.txt", "COM1.log", "LPT9", "bad?name", "bad*name", "trailing.", "trailing ",
                               QString("line\nfeed"), QString("tab\tname")};
    for (int index = 0; index < names.size(); ++index)
        QTest::newRow(qPrintable(QString::number(index))) << names[index];
}

void ExplorerFileOperationsTest::invalidNames()
{
    QFETCH(QString, name);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Explorer::FileOperations files(temp.path());
    QVERIFY(!files.createFile(temp.path(), name).ok);
    QVERIFY(!files.createDirectory(temp.path(), name).ok);
    QCOMPARE(QDir(temp.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void ExplorerFileOperationsTest::neverOverwrite()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath("original.md");
    const QString destination = temp.filePath("target");
    QVERIFY(QDir().mkdir(destination));
    QVERIFY(writeFile(source, "original content"));
    QVERIFY(writeFile(destination + "/original.md", "existing content"));
    Explorer::FileOperations files(temp.path());
    QVERIFY(!files.createFile(temp.path(), "original.md").ok);
    QVERIFY(!files.createDirectory(temp.path(), "original.md").ok);
    QVERIFY(!files.copy(source, destination).ok);
    QVERIFY(!files.move(source, destination).ok);
    QVERIFY(!files.rename(source, "target").ok);
    QCOMPARE(readFile(source), QByteArray("original content"));
    QCOMPARE(readFile(destination + "/original.md"), QByteArray("existing content"));
}

void ExplorerFileOperationsTest::renameAndMovePreserveContents()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Explorer::FileOperations files(temp.path());
    const auto folder = files.createDirectory(temp.path(), "source");
    QVERIFY(folder.ok);
    const auto target = files.createDirectory(temp.path(), "target");
    QVERIFY(target.ok);
    const QString original = folder.path + "/before.py";
    QVERIFY(writeFile(original, "print('hello')\n"));
    const auto renamed = files.rename(original, "after.py");
    QVERIFY2(renamed.ok, qPrintable(renamed.error));
    QVERIFY(!QFileInfo::exists(original));
    QCOMPARE(readFile(renamed.path), QByteArray("print('hello')\n"));
    const auto moved = files.move(folder.path, target.path);
    QVERIFY2(moved.ok, qPrintable(moved.error));
    QVERIFY(!QFileInfo::exists(folder.path));
    QCOMPARE(readFile(moved.path + "/after.py"), QByteArray("print('hello')\n"));
    const auto movedFile = files.move(moved.path + "/after.py", temp.path());
    QVERIFY2(movedFile.ok, qPrintable(movedFile.error));
    QCOMPARE(readFile(movedFile.path), QByteArray("print('hello')\n"));
}

void ExplorerFileOperationsTest::refuseTraversalAndWorkspaceRoot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir().mkdir(temp.filePath("workspace")));
    QVERIFY(QDir().mkdir(temp.filePath("outside")));
    const QString root = temp.filePath("workspace");
    const QString outside = temp.filePath("outside");
    Explorer::FileOperations files(root);
    QVERIFY(writeFile(root + "/source", "retained"));
    QVERIFY(writeFile(outside + "/untouched", "outside"));
    QVERIFY(!files.createFile(outside, "new").ok);
    QVERIFY(!files.rename(root + "/source", "../escape").ok);
    QVERIFY(!files.rename(root + "/source", "nested/escape").ok);
    QVERIFY(!files.move(root + "/source", outside).ok);
    QVERIFY(!files.copy(outside + "/untouched", root).ok);
    QVERIFY(!files.trash(outside + "/untouched").ok);
    QVERIFY(!files.trash(root).ok);
    QVERIFY(!files.rename(root, "renamed-workspace").ok);
    QCOMPARE(readFile(root + "/source"), QByteArray("retained"));
    QCOMPARE(readFile(outside + "/untouched"), QByteArray("outside"));
    QVERIFY(!QFileInfo::exists(temp.filePath("escape")));
}

void ExplorerFileOperationsTest::caseOnlyRenamePreservesFileAndFolder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir().mkdir(temp.filePath("folder")));
    QVERIFY(writeFile(temp.filePath("folder/example.py"), "case-preserved bytes"));
    Explorer::FileOperations files(temp.path());
    const auto file = files.rename(temp.filePath("folder/example.py"), "EXAMPLE.py");
    QVERIFY2(file.ok, qPrintable(file.error));
    QCOMPARE(QDir(temp.filePath("folder")).entryList(QDir::Files), QStringList{"EXAMPLE.py"});
    QCOMPARE(readFile(file.path), QByteArray("case-preserved bytes"));
    const auto folder = files.rename(temp.filePath("folder"), "FOLDER");
    QVERIFY2(folder.ok, qPrintable(folder.error));
    QCOMPARE(QDir(temp.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{"FOLDER"});
    QCOMPARE(readFile(folder.path + "/EXAMPLE.py"), QByteArray("case-preserved bytes"));
    QCOMPARE(QDir(temp.path()).entryList({".8bitpad-rename-*"}, QDir::Dirs | QDir::Hidden).size(), 0);
}

void ExplorerFileOperationsTest::refuseRecursiveDestination()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir().mkpath(temp.filePath("source/child")));
    Explorer::FileOperations files(temp.path());
    const QString source = temp.filePath("source");
    QVERIFY(!files.move(source, source + "/child").ok);
    QVERIFY(!files.copy(source, source + "/child").ok);
    QVERIFY(!files.move(source, source).ok);
    QVERIFY(!files.copy(source, source).ok);
    QVERIFY(QFileInfo(source + "/child").isDir());
    QVERIFY(!QFileInfo::exists(source + "/child/source"));
}

void ExplorerFileOperationsTest::copyDirectoryPreservesHiddenFiles()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir().mkpath(temp.filePath("source/nested")));
    QVERIFY(QDir().mkdir(temp.filePath("target")));
    QVERIFY(writeFile(temp.filePath("source/.config"), "hidden"));
    QVERIFY(writeFile(temp.filePath("source/nested/file.json"), "{\"value\":42}\n"));
    Explorer::FileOperations files(temp.path());
    const auto result = files.copy(temp.filePath("source"), temp.filePath("target"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(readFile(result.path + "/.config"), QByteArray("hidden"));
    QCOMPARE(readFile(result.path + "/nested/file.json"), QByteArray("{\"value\":42}\n"));
    QCOMPARE(readFile(temp.filePath("source/nested/file.json")), QByteArray("{\"value\":42}\n"));
    QCOMPARE(QDir(temp.filePath("target")).entryList({".8bitpad-copy-*"}, QDir::Dirs | QDir::Hidden).size(), 0);
}

void ExplorerFileOperationsTest::cancelledCopyLeavesSourceAndNoPartialDestination()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir().mkdir(temp.filePath("target")));
    const QByteArray payload(4 * 1024 * 1024, 'x');
    QVERIFY(writeFile(temp.filePath("large.bin"), payload));
    Explorer::FileOperations files(temp.path());
    int checks = 0;
    const auto result = files.copy(temp.filePath("large.bin"), temp.filePath("target"), [&checks] { return ++checks >= 4; });
    QVERIFY(!result.ok);
    QVERIFY(result.cancelled);
    QVERIFY(!QFileInfo::exists(temp.filePath("target/large.bin")));
    QCOMPARE(readFile(temp.filePath("large.bin")), payload);
    QCOMPARE(QDir(temp.filePath("target")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}

void ExplorerFileOperationsTest::rejectLinkEscapesAndCleanFailedCopy()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir().mkpath(temp.filePath("workspace/source")));
    QVERIFY(QDir().mkdir(temp.filePath("workspace/target")));
    QVERIFY(QDir().mkdir(temp.filePath("outside")));
    QVERIFY(writeFile(temp.filePath("outside/secret"), "untouched"));
    QVERIFY(writeFile(temp.filePath("workspace/source/a-first.txt"), "safe content"));
    std::error_code error;
    std::filesystem::create_directory_symlink(nativePath(temp.filePath("outside")),
                                              nativePath(temp.filePath("workspace/source/z-link")), error);
    if (error) {
#ifdef Q_OS_WIN
        // Junctions exercise the same escape boundary without requiring the
        // Windows symbolic-link privilege. Both endpoints are disposable fixtures.
        QProcess junction;
        junction.start(qEnvironmentVariable("SystemRoot") + "/System32/cmd.exe",
                       {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(temp.filePath("workspace/source/z-link")),
                        QDir::toNativeSeparators(temp.filePath("outside"))});
        if (!junction.waitForFinished(10000) || junction.exitCode() != 0)
            QSKIP("Creating symbolic links and junctions is not permitted on this machine.");
        QVERIFY(QFileInfo(temp.filePath("workspace/source/z-link")).isJunction());
#else
        QSKIP("Creating a real symbolic link is not permitted on this machine.");
#endif
    }
    Explorer::FileOperations files(temp.filePath("workspace"));
    const QString linked = temp.filePath("workspace/source/z-link");
    QVERIFY(!files.createFile(linked, "escape").ok);
    QVERIFY(!files.rename(linked + "/secret", "renamed").ok);
    QVERIFY(!files.move(linked + "/secret", temp.filePath("workspace/target")).ok);
    QVERIFY(!files.trash(linked + "/secret").ok);
    const auto result = files.copy(temp.filePath("workspace/source"), temp.filePath("workspace/target"));
    QVERIFY(!result.ok);
    QVERIFY(!QFileInfo::exists(temp.filePath("workspace/target/source")));
    QCOMPARE(QDir(temp.filePath("workspace/target")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
    QCOMPARE(readFile(temp.filePath("outside/secret")), QByteArray("untouched"));
    QCOMPARE(readFile(temp.filePath("workspace/source/a-first.txt")), QByteArray("safe content"));
}

QTEST_GUILESS_MAIN(ExplorerFileOperationsTest)
#include "ExplorerFileOperationsTest.moc"
