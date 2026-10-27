// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileAssociations.h"
#include "ShellIntegration.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace FileAssociations;

namespace {
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

Services windows(QSettings &store)
{
    Services services;
    services.platform = Platform::Windows;
    services.executable = "C:/Program Files/8bitPad/8bitPad.exe";
    services.registry = &store;
    return services;
}

Services linuxServices(const QTemporaryDir &temp)
{
    Services services;
    services.platform = Platform::Linux;
    services.executable = "/opt/8bitPad edition/8bitPad";
    services.dataHome = temp.filePath("xdg-data");
    services.configHome = temp.filePath("xdg-config");
    services.findExecutable = [](const QString &program) { return "/fake-tools/" + program; };
    services.run = [](const QString &, const QStringList &args) {
        return CommandResult{true, 0, args.value(0) == "query" ? QString("8bitpad.desktop\n") : QString(), {}};
    };
    return services;
}
}

class FileAssociationsTest : public QObject
{
    Q_OBJECT
private slots:
    void parsesCustomBatchAtomically();
    void catalogContainsOnlyUniqueTextTypes();
    void windowsBatchPreservesUserChoiceAndOpensSettingsOnce();
    void windowsContextToggleDoesNotChangeAssociations();
    void windowsCleanupIncludesCustomAndPreservesSettings();
    void windowsDefaultUiFailureIsReported();
    void linuxDeduplicatesAndConfirmsMimeDefaults();
    void linuxFailuresNeverClaimDefaults();
    void linuxCustomRegistrationAndCleanupStayInXdg();
    void linuxContextToggleIsSeparate();
};

void FileAssociationsTest::parsesCustomBatchAtomically()
{
    const auto result = parseExtensions(".TXT, *.py;sql\njson txt");
    QVERIFY(result.valid());
    QCOMPARE(result.extensions, QStringList({"txt", "py", "sql", "json"}));
    for (const QString &invalid : {"txt ../evil", "*.exe", "txt *", "txt foo/bar", "txt .", "txt tar.gz", "txt bad\\name"}) {
        const auto parsed = parseExtensions(invalid);
        QVERIFY2(!parsed.valid(), qPrintable(invalid));
        QVERIFY(parsed.extensions.isEmpty());
    }
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    QVERIFY(!Backend(windows(store)).apply({"txt", "../escape"}, false).ok());
    QVERIFY(store.allKeys().isEmpty());
}

void FileAssociationsTest::catalogContainsOnlyUniqueTextTypes()
{
    QSet<QString> seen;
    for (const auto &group : catalog()) {
        QVERIFY(!group.title.isEmpty());
        for (const QString &extension : group.extensions) {
            QVERIFY2(!seen.contains(extension), qPrintable(extension));
            seen.insert(extension);
            const auto parsed = parseExtensions(extension);
            QVERIFY(parsed.valid());
            QCOMPARE(parsed.extensions, QStringList{extension});
        }
    }
    QVERIFY(seen.size() > 100);
    QVERIFY(seen.contains("txt") && seen.contains("cpp") && seen.contains("py") && seen.contains("yaml"));
    QVERIFY(!seen.contains("exe") && !seen.contains("png") && !seen.contains("url"));
}

void FileAssociationsTest::windowsBatchPreservesUserChoiceAndOpensSettingsOnce()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    const QString protectedKey = "Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/.txt/UserChoice/";
    store.setValue(protectedKey + "ProgId", "Other.Text");
    store.setValue(protectedKey + "Hash", "unchanged");
    store.setValue("Software/Classes/.txt/.", "Other.Text");
    int opens = 0;
    int notifications = 0;
    QUrl openedUrl;
    auto services = windows(store);
    services.openDefaults = [&](const QUrl &url) {
        ++opens;
        openedUrl = url;
        return true;
    };
    services.associationsChanged = [&] { ++notifications; };
    Backend backend(services);
    const auto result = backend.apply({"txt", "py", "TXT"}, true);
    QVERIFY(result.ok());
    QCOMPARE(result.registeredExtensions, QStringList({"txt", "py"}));
    QVERIFY(result.defaultMimeTypes.isEmpty());
    QVERIFY(result.defaultsUiOpened);
    QCOMPARE(opens, 1);
    QCOMPARE(notifications, 1);
    QCOMPARE(openedUrl.toString(), QString("ms-settings:defaultapps?registeredAppUser=8bitPad"));
    QCOMPARE(store.value(protectedKey + "ProgId").toString(), QString("Other.Text"));
    QCOMPARE(store.value(protectedKey + "Hash").toString(), QString("unchanged"));
    QCOMPARE(store.value("Software/Classes/.txt/.").toString(), QString("Other.Text"));
    QVERIFY(store.contains("Software/Classes/.py/OpenWithProgids/8bitPad.py"));
    QCOMPARE(store.value("Software/8bitPad/Capabilities/FileAssociations/.txt").toString(), QString("8bitPad.txt"));
    QVERIFY(!backend.contextMenuInstalled());
}

void FileAssociationsTest::windowsContextToggleDoesNotChangeAssociations()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    Backend backend(windows(store));
    QVERIFY(backend.setContextMenuEnabled(true).ok());
    QVERIFY(backend.contextMenuInstalled());
    QVERIFY(backend.registeredExtensions().isEmpty());
    QVERIFY(!store.contains("Software/RegisteredApplications/8bitPad"));
    QVERIFY(backend.apply({"txt", "customnote"}).ok());
    QVERIFY(backend.setContextMenuEnabled(false).ok());
    QVERIFY(!backend.contextMenuInstalled());
    QCOMPARE(backend.registeredExtensions(), QStringList({"txt", "customnote"}));
    QVERIFY(store.contains("Software/Classes/.customnote/OpenWithProgids/8bitPad.customnote"));
}

void FileAssociationsTest::windowsCleanupIncludesCustomAndPreservesSettings()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    store.setValue("Software/8bitPad/Theme", "ayu");
    store.setValue("Software/Classes/.customnote/OpenWithProgids/Other.Editor", "");
    store.setValue("Software/Classes/.customnote/.", "Other.Editor");
    Backend backend(windows(store));
    QVERIFY(backend.apply({"txt", "customnote"}).ok());
    QVERIFY(backend.removeOwnedRegistration().ok());
    QCOMPARE(store.value("Software/8bitPad/Theme").toString(), QString("ayu"));
    QCOMPARE(store.value("Software/Classes/.customnote/.").toString(), QString("Other.Editor"));
    QVERIFY(store.contains("Software/Classes/.customnote/OpenWithProgids/Other.Editor"));
    QVERIFY(!store.contains("Software/Classes/.customnote/OpenWithProgids/8bitPad.customnote"));
    QVERIFY(!store.contains("Software/Classes/8bitPad.customnote/shell/open/command/."));
    QVERIFY(backend.registeredExtensions().isEmpty());
}

void FileAssociationsTest::windowsDefaultUiFailureIsReported()
{
    QTemporaryDir temp;
    QSettings store(temp.filePath("registry.ini"), QSettings::IniFormat);
    auto services = windows(store);
    int attempts = 0;
    services.openDefaults = [&](const QUrl &) { ++attempts; return false; };
    const auto result = Backend(services).apply({"txt", "py"}, true);
    QVERIFY(!result.ok());
    QCOMPARE(attempts, 2); // Specific page then generic fallback, not once per extension.
    QVERIFY(!result.defaultsUiOpened);
    QCOMPARE(result.registeredExtensions.size(), 2);
    QVERIFY(result.defaultMimeTypes.isEmpty());
}

void FileAssociationsTest::linuxDeduplicatesAndConfirmsMimeDefaults()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    QList<QStringList> defaults;
    services.run = [&](const QString &program, const QStringList &args) {
        if (program.endsWith("xdg-mime") && args.value(0) == "default") defaults << args;
        return CommandResult{true, 0, args.value(0) == "query" ? QString("8bitpad.desktop\n") : QString(), {}};
    };
    const auto result = Backend(services).apply({"txt", "log", "ini", "py"}, true);
    QVERIFY2(result.ok(), qPrintable(result.errors.join('\n')));
    QCOMPARE(defaults.size(), 2);
    QCOMPARE(result.defaultMimeTypes, QStringList({"text/plain", "text/x-python"}));
    const QByteArray entry = readFile(services.dataHome + "/applications/8bitpad.desktop");
    QVERIFY(entry.contains("text/plain"));
    QVERIFY(entry.contains("text/x-python"));
    QVERIFY(!entry.contains("inode/directory"));
    const QByteArray globs = readFile(services.dataHome + "/mime/packages/8bitpad-types.xml");
    QVERIFY(globs.contains("*.txt") && globs.contains("*.log") && globs.contains("*.py"));
    for (const auto &args : defaults) QVERIFY(!args.contains("inode/directory"));
}

void FileAssociationsTest::linuxFailuresNeverClaimDefaults()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    services.run = [](const QString &program, const QStringList &args) {
        if (program.endsWith("xdg-mime") && args.value(0) == "default") return CommandResult{true, 4, {}, "desktop refused"};
        return CommandResult{true, 0, {}, {}};
    };
    const auto refused = Backend(services).apply({"txt"}, true);
    QVERIFY(!refused.ok());
    QVERIFY(refused.defaultMimeTypes.isEmpty());
    QCOMPARE(refused.registeredExtensions, QStringList{"txt"});
    services.run = [](const QString &, const QStringList &args) {
        return CommandResult{true, 0, args.value(0) == "query" ? QString("other.desktop") : QString(), {}};
    };
    const auto unchanged = Backend(services).apply({"txt"}, true);
    QVERIFY(!unchanged.ok());
    QVERIFY(unchanged.defaultMimeTypes.isEmpty());
    services.findExecutable = [](const QString &) { return QString(); };
    const auto missing = Backend(services).apply({"txt"}, true);
    QVERIFY(!missing.ok());
    QVERIFY(missing.defaultMimeTypes.isEmpty());
}

void FileAssociationsTest::linuxCustomRegistrationAndCleanupStayInXdg()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    services.executable = "/opt/8bitPad % edition/8bitPad";
    Backend backend(services);
    const auto result = backend.apply({"customnote", "md", "jsonc", "tsx", "ts"});
    QVERIFY2(result.ok(), qPrintable(result.errors.join('\n')));
    const QByteArray entry = readFile(services.dataHome + "/applications/8bitpad.desktop");
    QVERIFY(entry.contains("application/x-8bitpad-customnote"));
    QVERIFY(entry.contains("8bitPad %% edition"));
    QVERIFY(QFileInfo::exists(services.dataHome + "/mime/packages/8bitpad-types.xml"));
    const QByteArray aliases = readFile(services.dataHome + "/mime/packages/8bitpad-types.xml");
    QVERIFY(aliases.contains("*.jsonc") && aliases.contains("*.tsx") && aliases.contains("*.ts"));
    const QString mimeapps = services.configHome + "/mimeapps.list";
    QVERIFY(writeFile(mimeapps, "# keep comment\n[Default Applications]\ntext/plain=8bitpad.desktop;other.desktop;\nimage/png=viewer.desktop;\n[Added Associations]\ntext/markdown=other.desktop;8bitpad.desktop;\n"));
    QVERIFY(writeFile(services.dataHome + "/mime/packages/other-app.xml", "keep"));
    QVERIFY(backend.removeOwnedRegistration().ok());
    const QByteArray after = readFile(mimeapps);
    QVERIFY(after.contains("text/plain=other.desktop;"));
    QVERIFY(after.contains("image/png=viewer.desktop;"));
    QVERIFY(after.contains("# keep comment"));
    QVERIFY(!after.contains("8bitpad.desktop"));
    QCOMPARE(readFile(services.dataHome + "/mime/packages/other-app.xml"), QByteArray("keep"));
    QVERIFY(!QFileInfo::exists(services.dataHome + "/applications/8bitpad.desktop"));
}

void FileAssociationsTest::linuxContextToggleIsSeparate()
{
    QTemporaryDir temp;
    auto services = linuxServices(temp);
    Backend backend(services);
    QVERIFY(backend.setContextMenuEnabled(true).ok());
    QVERIFY(backend.contextMenuInstalled());
    QVERIFY(backend.registeredExtensions().isEmpty());
    const QByteArray folder = readFile(services.dataHome + "/applications/8bitpad-folder.desktop");
    QVERIFY(folder.contains("inode/directory"));
    QVERIFY(backend.apply({"txt"}).ok());
    const QByteArray association = readFile(services.dataHome + "/applications/8bitpad.desktop");
    QVERIFY(backend.setContextMenuEnabled(false).ok());
    QVERIFY(!backend.contextMenuInstalled());
    QCOMPARE(readFile(services.dataHome + "/applications/8bitpad.desktop"), association);
    QCOMPARE(backend.registeredExtensions(), QStringList{"txt"});
}

QTEST_GUILESS_MAIN(FileAssociationsTest)
#include "FileAssociationsTest.moc"
