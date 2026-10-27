// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExternalTerminals.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

using namespace ExternalTerminals;

namespace {
class EnvironmentValue
{
public:
    EnvironmentValue(const char *name, const QByteArray &value)
        : name(name), wasSet(qEnvironmentVariableIsSet(name)), previous(qgetenv(name)) { qputenv(name, value); }
    ~EnvironmentValue() { if (wasSet) qputenv(name.constData(), previous); else qunsetenv(name.constData()); }
private:
    QByteArray name;
    bool wasSet;
    QByteArray previous;
};

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QString executable() { return QCoreApplication::applicationFilePath(); }

#ifdef Q_OS_WIN
bool writeShortcut(const QString &path, const QString &target)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW *link = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        IPersistFile *file = nullptr;
        if (SUCCEEDED(link->SetPath(reinterpret_cast<LPCWSTR>(target.utf16())))
            && SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            ok = SUCCEEDED(file->Save(reinterpret_cast<LPCWSTR>(path.utf16()), TRUE));
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return ok;
}
#endif
}

class ExternalTerminalsTest : public QObject
{
    Q_OBJECT
private slots:
    void settingsRoundTripPreservesMissingSelection();
    void argumentLinesStayLiteral();
    void launchArgumentsDoNotInterpretFolder();
    void windowsTerminalUsesFixedRelativeDirectory();
    void consoleFlagAndCustomChoice();
    void invalidPathsAndBatchFilesAreRejected();
    void detachedHelperReceivesExactArgumentsAndWorkingDirectory();
    void discoveryDoesNotLaunchAndFiltersInjectedRuntime();
#ifndef Q_OS_WIN
    void linuxDesktopEntriesRemainStructured();
#endif
};

void ExternalTerminalsTest::settingsRoundTripPreservesMissingSelection()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QSettings storage(temp.filePath("settings.ini"), QSettings::IniFormat);
    Settings expected;
    expected.terminalId = "missing-installed-app";
    expected.customName = "My shell ü";
    expected.customProgram = "C:/a path/terminal.exe";
    expected.customArguments = {"--folder", "{folder}", "--literal=hello world", "$(nothing runs)"};
    expected.customNewConsole = true;
    writeSettings(&storage, expected);
    storage.sync();
    const Settings actual = readSettings(&storage);
    QCOMPARE(actual.terminalId, expected.terminalId);
    QCOMPARE(actual.customName, expected.customName);
    QCOMPARE(actual.customProgram, expected.customProgram);
    QCOMPARE(actual.customArguments, expected.customArguments);
    QCOMPARE(actual.customNewConsole, true);
    const auto missing = terminalForSettings(actual, {});
    QCOMPARE(missing.id, expected.terminalId);
    QVERIFY(missing.program.isEmpty());
    QCOMPARE(readSettings(nullptr).terminalId, QString("auto"));
}

void ExternalTerminalsTest::argumentLinesStayLiteral()
{
    const QString input = "--folder\r\n{folder}\n\n  keep spaces  \n\"quotes are literal\"\n$(not-a-command)\n";
    const QStringList expected = {"--folder", "{folder}", "  keep spaces  ", "\"quotes are literal\"", "$(not-a-command)"};
    QCOMPARE(argumentsFromText(input), expected);
    QCOMPARE(argumentsFromText(argumentsToText(expected)), expected);
}

void ExternalTerminalsTest::launchArgumentsDoNotInterpretFolder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString folder = temp.filePath("space ü ç ; & % $ ( ) ! [ ]");
    QVERIFY(QDir().mkdir(folder));
    const Terminal terminal{"custom", "Fixture", executable(), {"--directory", "{folder}", "prefix={folder}", "literal ; & $(x)"}, Kind::Custom};
    const LaunchSpec spec = buildLaunch(terminal, folder);
    QVERIFY2(spec.valid(), qPrintable(spec.error));
    QCOMPARE(spec.program, executable());
    QCOMPARE(spec.workingDirectory, folder);
    QCOMPARE(spec.arguments, QStringList({"--directory", QDir::toNativeSeparators(folder),
                                         "prefix=" + QDir::toNativeSeparators(folder), "literal ; & $(x)"}));
    QVERIFY(!spec.newConsole);
}

void ExternalTerminalsTest::windowsTerminalUsesFixedRelativeDirectory()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString folder = temp.filePath("folder; new-tab & unicode ü");
    QVERIFY(QDir().mkdir(folder));
    const Terminal terminal{"windows-terminal", "Windows Terminal", executable(), {"discard-existing-options"}, Kind::WindowsTerminal};
    const LaunchSpec spec = buildLaunch(terminal, folder);
    QVERIFY2(spec.valid(), qPrintable(spec.error));
    QCOMPARE(spec.arguments, QStringList({"-w", "new", "new-tab", "-d", "."}));
    QCOMPARE(spec.workingDirectory, folder);
    for (const QString &argument : spec.arguments) QVERIFY(!argument.contains(';'));
    QVERIFY(!spec.newConsole);
}

void ExternalTerminalsTest::consoleFlagAndCustomChoice()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Terminal terminal{"powershell", "PowerShell", executable(), {"-NoLogo", "-NoExit"}, Kind::WindowsConsole};
    const auto spec = buildLaunch(terminal, temp.path());
    QVERIFY(spec.valid());
#ifdef Q_OS_WIN
    QVERIFY(spec.newConsole);
#else
    QVERIFY(!spec.newConsole);
#endif
    Settings settings;
    settings.terminalId = "custom";
    settings.customName = "My terminal";
    settings.customProgram = executable();
    settings.customArguments = {"--directory", "{folder}"};
    settings.customNewConsole = true;
    const auto custom = terminalForSettings(settings, {});
    QCOMPARE(custom.name, settings.customName);
    QCOMPARE(custom.kind, Kind::Custom);
    QVERIFY(custom.newConsole);
    const QList<Terminal> available{terminal};
    QCOMPARE(terminalForSettings(Settings{}, available).id, terminal.id);
}

void ExternalTerminalsTest::invalidPathsAndBatchFilesAreRejected()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Terminal terminal{"custom", "Fixture", executable(), {}, Kind::Custom};
    QVERIFY(!buildLaunch(terminal, temp.filePath("missing")).valid());
    QVERIFY(writeFile(temp.filePath("a-file"), "content"));
    QVERIFY(!buildLaunch(terminal, temp.filePath("a-file")).valid());
    terminal.program = temp.filePath("missing.exe");
    QVERIFY(!buildLaunch(terminal, temp.path()).valid());
    terminal.program = "not-an-executable --command text";
    QVERIFY(!buildLaunch(terminal, temp.path()).valid());
    terminal.program = "relative/path/terminal.exe";
    QVERIFY(!buildLaunch(terminal, temp.path()).valid());
    terminal.program = executable();
    terminal.arguments = {QString("bad") + QChar::Null + "argument"};
    QVERIFY(!buildLaunch(terminal, temp.path()).valid());
    QVERIFY(!launch(LaunchSpec{}).ok);
#ifdef Q_OS_WIN
    const QString batch = temp.filePath("terminal.cmd");
    QVERIFY(writeFile(batch, "@echo off\r\n"));
    terminal.program = batch;
    terminal.arguments.clear();
    QVERIFY(!buildLaunch(terminal, temp.path()).valid());
#endif
}

void ExternalTerminalsTest::detachedHelperReceivesExactArgumentsAndWorkingDirectory()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString folder = temp.filePath("selected ü ; & % $ ( ) ! folder");
    QVERIFY(QDir().mkdir(folder));
    const QString report = temp.filePath("child.json");
    const QStringList literal = {"{folder}", "arg with spaces", "$(not evaluated); & % !", "a\"b", "back\\slash\\", ""};
    QStringList arguments = {"--terminal-test-helper", report};
    arguments << literal;
    Terminal terminal{"custom", "Detached test helper", executable(), arguments, Kind::Custom};
    EnvironmentValue marker("BIT8PAD_TERMINAL_TEST_MARKER", "inherited value");
    const QString appPlugins = QCoreApplication::applicationDirPath() + "/plugins";
    // The main build intentionally places TEMP below the test executable.
    // Use a sibling path (it need not exist) to model unrelated Qt plugins.
    const QString unrelatedPlugins = QDir(QFileInfo(QCoreApplication::applicationDirPath()).absolutePath())
                                         .filePath("8bitpad-unrelated-test-plugins");
    EnvironmentValue plugins("QT_PLUGIN_PATH", (appPlugins + QDir::listSeparator() + unrelatedPlugins).toUtf8());
    const auto spec = buildLaunch(terminal, folder);
    QVERIFY2(spec.valid(), qPrintable(spec.error));
    QVERIFY(!spec.newConsole); // Automated tests never open a visible shell/console.
    const auto result = launch(spec);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.processId > 0);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(report), 10000);
    QFile file(report);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value("cwd").toString(), folder);
    QStringList expected = literal;
    expected[0] = QDir::toNativeSeparators(folder);
    QCOMPARE(object.value("arguments").toArray(), QJsonArray::fromStringList(expected));
    QCOMPARE(object.value("marker").toString(), QString("inherited value"));
    QCOMPARE(object.value("plugins").toString(), unrelatedPlugins);
}

void ExternalTerminalsTest::discoveryDoesNotLaunchAndFiltersInjectedRuntime()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
#ifdef Q_OS_WIN
    const QString fakeBin = temp.filePath("visible-installed-bin");
    const QString injectedBin = temp.filePath(".cache/codex-runtimes/fake/bin");
    QVERIFY(QDir().mkpath(fakeBin));
    QVERIFY(QDir().mkpath(injectedBin));
    QVERIFY(QFile::copy(executable(), fakeBin + "/wt.exe"));
    QVERIFY(QFile::copy(executable(), injectedBin + "/pwsh.exe"));
    const QString uninstallTarget = fakeBin + "/msiexec.exe";
    QVERIFY(QFile::copy(executable(), uninstallTarget));
    const QString startMenu = temp.filePath("Microsoft/Windows/Start Menu/Programs/8bitShell");
    QVERIFY(QDir().mkpath(startMenu));
    QVERIFY(writeShortcut(startMenu + "/Uninstall 8bitShell.lnk", uninstallTarget));
    EnvironmentValue programData("ProgramData", temp.path().toUtf8());
    EnvironmentValue path("PATH", (injectedBin + ';' + fakeBin).toUtf8());
    const auto terminals = discover(true);
    bool foundFakeTerminal = false;
    for (const auto &terminal : terminals) {
        QVERIFY(!terminal.program.contains("/.cache/codex-runtimes/"));
        QVERIFY(QFileInfo(terminal.program).fileName().compare("msiexec.exe", Qt::CaseInsensitive) != 0);
        QVERIFY(!terminal.name.contains("Uninstall", Qt::CaseInsensitive));
        if (terminal.id == "windows-terminal") {
            QCOMPARE(terminal.program, fakeBin + "/wt.exe");
            foundFakeTerminal = true;
        }
    }
    QVERIFY(foundFakeTerminal);
    QCOMPARE(discover().size(), terminals.size());
#else
    const auto terminals = discover(true);
    for (const auto &terminal : terminals) {
        QVERIFY(!terminal.id.isEmpty());
        QVERIFY(QFileInfo(terminal.program).isExecutable());
    }
#endif
}

#ifndef Q_OS_WIN
void ExternalTerminalsTest::linuxDesktopEntriesRemainStructured()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString applications = temp.filePath("applications");
    QVERIFY(QDir().mkdir(applications));
    const QString desktop = QString("[Desktop Entry]\nType=Application\nName=Fixture terminal\nCategories=System;TerminalEmulator;\nExec=\"%1\" --literal \"space ; & text\" %% %F -e sh -c \"touch not-run\"\n").arg(executable());
    QVERIFY(writeFile(applications + "/fixture-terminal.desktop", desktop.toUtf8()));
    QVERIFY(writeFile(applications + "/unsafe-wrapper.desktop", "[Desktop Entry]\nType=Application\nName=No shell wrapper\nCategories=TerminalEmulator;\nExec=/bin/sh -c \"echo forbidden\"\n"));
    EnvironmentValue data("XDG_DATA_HOME", temp.path().toUtf8());
    EnvironmentValue systemData("XDG_DATA_DIRS", temp.filePath("empty").toUtf8());
    const auto terminals = discover(true);
    bool found = false;
    for (const auto &terminal : terminals) {
        QVERIFY(terminal.id != "desktop:unsafe-wrapper.desktop");
        if (terminal.id == "desktop:fixture-terminal.desktop") {
            found = true;
            QVERIFY(terminal.arguments.isEmpty());
        }
    }
    QVERIFY(found);
}
#endif

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() >= 3 && arguments[1] == "--terminal-test-helper") {
        const QJsonObject report{{"cwd", QDir::currentPath()}, {"arguments", QJsonArray::fromStringList(arguments.mid(3))},
                                 {"marker", qEnvironmentVariable("BIT8PAD_TERMINAL_TEST_MARKER")},
                                 {"plugins", qEnvironmentVariable("QT_PLUGIN_PATH")}};
        QSaveFile file(arguments[2]);
        if (!file.open(QIODevice::WriteOnly)) return 2;
        if (file.write(QJsonDocument(report).toJson()) < 0 || !file.commit()) return 3;
        return 0;
    }
    ExternalTerminalsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ExternalTerminalsTest.moc"
