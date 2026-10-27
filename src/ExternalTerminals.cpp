// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExternalTerminals.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

namespace ExternalTerminals {
namespace {
const QString settingsPrefix = QStringLiteral("ExternalTerminal/");

bool injectedRuntime(const QString &path)
{
    return QDir::fromNativeSeparators(path).toLower().contains("/.cache/codex-runtimes/");
}

QString executablePath(const QString &program, bool discovery = false)
{
    if (program.isEmpty() || program.contains(QChar::Null)) return {};
    QString result;
    if (QDir::isAbsolutePath(program)) {
        result = QDir::cleanPath(program);
    } else if (!program.contains('/') && !program.contains('\\')) {
        QStringList paths = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
        QStringList search;
        for (const QString &entry : paths) {
            // Never search the editor's current workspace for an executable.
            if (!QDir::isAbsolutePath(entry) || (discovery && injectedRuntime(entry))) continue;
            search << entry;
        }
        if (!search.isEmpty()) result = QStandardPaths::findExecutable(program, search);
    }
    const QFileInfo info(result);
    if (result.isEmpty() || !info.exists() || !info.isFile() || !info.isExecutable()
        || (discovery && injectedRuntime(result))) return {};
#ifdef Q_OS_WIN
    // Batch files require cmd's command-text grammar. This backend only starts
    // executables and passes a structured argv list directly to CreateProcess.
    const QString suffix = info.suffix().toLower();
    if (suffix != "exe" && suffix != "com") return {};
#endif
    return info.absoluteFilePath();
}

QString stablePathId(const QString &prefix, const QString &path)
{
    return prefix + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

Terminal adapter(const QString &program, const QString &fallbackName = {})
{
    const QString base = QFileInfo(program).completeBaseName().toLower();
    Terminal terminal;
    terminal.program = program;
    terminal.id = stablePathId("terminal-", program);
    terminal.name = fallbackName.isEmpty() ? QFileInfo(program).completeBaseName() : fallbackName;
    if (base == "wt" || base == "windowsterminal") {
        terminal.id = "windows-terminal";
        terminal.name = "Windows Terminal";
        terminal.kind = Kind::WindowsTerminal;
    } else if (base == "pwsh") {
        terminal.id = "powershell";
        terminal.name = "PowerShell";
        terminal.kind = Kind::WindowsConsole;
        terminal.arguments = {"-NoLogo", "-NoExit"};
    } else if (base == "powershell") {
        terminal.id = "windows-powershell";
        terminal.name = "Windows PowerShell";
        terminal.kind = Kind::WindowsConsole;
        terminal.arguments = {"-NoLogo", "-NoExit"};
    } else if (base == "cmd") {
        terminal.id = "command-prompt";
        terminal.name = QObject::tr("Command Prompt");
        terminal.kind = Kind::WindowsConsole;
        terminal.arguments = {"/d"};
    } else if (base == "8bitshell") {
        terminal.id = base;
        terminal.name = "8bitShell";
        terminal.arguments = {"--working-directory", "{folder}"};
    } else {
        // These options accept one directory argument, never shell command text.
        const QHash<QString, QStringList> directories = {
            {"gnome-terminal", {"--working-directory={folder}"}},
            {"kgx", {"--working-directory={folder}"}},
            {"konsole", {"--workdir", "{folder}"}},
            {"xfce4-terminal", {"--working-directory={folder}"}},
            {"mate-terminal", {"--working-directory={folder}"}},
            {"lxterminal", {"--working-directory={folder}"}},
            {"tilix", {"--working-directory={folder}"}},
            {"kitty", {"--directory", "{folder}"}},
            {"alacritty", {"--working-directory", "{folder}"}},
            {"wezterm", {"start", "--cwd", "{folder}"}},
            {"foot", {"--working-directory={folder}"}},
            {"footclient", {"--working-directory={folder}"}},
            {"urxvt", {"-cd", "{folder}"}}
        };
        if (directories.contains(base)) {
            terminal.id = base;
            terminal.arguments = directories.value(base);
        } else if (base == "xterm" || base == "x-terminal-emulator") {
            terminal.id = base;
        }
    }
    return terminal;
}

void append(QList<Terminal> &terminals, Terminal terminal)
{
    terminal.program = executablePath(terminal.program, true);
    if (terminal.program.isEmpty()) return;
    for (const Terminal &existing : terminals) {
#ifdef Q_OS_WIN
        constexpr auto sensitivity = Qt::CaseInsensitive;
#else
        constexpr auto sensitivity = Qt::CaseSensitive;
#endif
        if (terminal.id == existing.id || terminal.program.compare(existing.program, sensitivity) == 0)
            return;
    }
    terminals << terminal;
}

#ifdef Q_OS_WIN
bool knownTerminalTarget(const QString &path)
{
    static const QSet<QString> names = {"wt", "windowsterminal", "8bitshell", "pwsh", "powershell", "cmd",
                                       "wezterm", "alacritty", "kitty", "mintty", "conemu", "conemu64", "tabby", "hyper"};
    return names.contains(QFileInfo(path).completeBaseName().toLower());
}

QString appPath(HKEY hive, const QString &name)
{
    const QString keyName = "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + name;
    HKEY key = nullptr;
    if (RegOpenKeyExW(hive, reinterpret_cast<LPCWSTR>(keyName.utf16()), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return {};
    DWORD type = 0;
    DWORD bytes = 0;
    LONG status = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &bytes);
    QString result;
    if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > 0 && bytes <= 65536) {
        QByteArray data(int(bytes) + int(sizeof(wchar_t)), '\0');
        status = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE *>(data.data()), &bytes);
        if (status == ERROR_SUCCESS) {
            result = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(data.constData()));
            if (type == REG_EXPAND_SZ) {
                const DWORD length = ExpandEnvironmentStringsW(reinterpret_cast<LPCWSTR>(result.utf16()), nullptr, 0);
                if (length > 0 && length < 32768) {
                    std::wstring expanded(length, L'\0');
                    ExpandEnvironmentStringsW(reinterpret_cast<LPCWSTR>(result.utf16()), expanded.data(), length);
                    result = QString::fromWCharArray(expanded.c_str());
                } else result.clear();
            }
        }
    }
    RegCloseKey(key);
    if (result.startsWith('"') && result.endsWith('"')) result = result.mid(1, result.size() - 2);
    return result;
}

QString shortcutTarget(const QString &path)
{
    IShellLinkW *link = nullptr;
    QString result;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return {};
    IPersistFile *file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        if (SUCCEEDED(file->Load(reinterpret_cast<LPCWSTR>(path.utf16()), STGM_READ))) {
            wchar_t target[32768] = {};
            // GetPath reads the stored target; Resolve could contact a network
            // share or invoke Windows Installer and is deliberately not used.
            if (SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)))
                result = QString::fromWCharArray(target);
        }
        file->Release();
    }
    link->Release();
    return result;
}

QList<Terminal> discoverWindows()
{
    QList<Terminal> result;
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString programs = qEnvironmentVariable("ProgramFiles");
    const QString programs32 = qEnvironmentVariable("ProgramFiles(x86)");
    const QString system = qEnvironmentVariable("SystemRoot");
    const QStringList candidates = {
        "wt.exe", local + "/Microsoft/WindowsApps/wt.exe",
        programs + "/8bitShell/bin/8bitShell.exe", local + "/Programs/8bitShell/bin/8bitShell.exe", "8bitShell.exe",
        programs + "/PowerShell/7/pwsh.exe", programs32 + "/PowerShell/7/pwsh.exe", "pwsh.exe",
        system + "/System32/WindowsPowerShell/v1.0/powershell.exe", system + "/System32/cmd.exe"
    };
    for (const QString &candidate : candidates) {
        const QString program = executablePath(candidate, true);
        if (!program.isEmpty()) append(result, adapter(program));
    }
    const QStringList appNames = {"wt.exe", "WindowsTerminal.exe", "8bitShell.exe", "pwsh.exe", "powershell.exe", "cmd.exe"};
    for (const QString &name : appNames)
        for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
            const QString program = appPath(hive, name);
            if (!program.isEmpty() && knownTerminalTarget(program)) append(result, adapter(program));
        }

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE) {
        QStringList roots = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        const QString appData = qEnvironmentVariable("APPDATA");
        const QString programData = qEnvironmentVariable("ProgramData");
        if (!appData.isEmpty()) roots << appData + "/Microsoft/Windows/Start Menu/Programs";
        if (!programData.isEmpty()) roots << programData + "/Microsoft/Windows/Start Menu/Programs";
        roots.removeDuplicates();
        const QStringList subfolders = {QString(), "8bitPrograms", "8bitShell", "PowerShell", "Windows PowerShell", "Windows Terminal", "System Tools"};
        int inspected = 0;
        for (const QString &root : roots) for (const QString &subfolder : subfolders) {
            const QDir directory(subfolder.isEmpty() ? root : QDir(root).filePath(subfolder));
            const auto links = directory.entryInfoList({"*.lnk"}, QDir::Files, QDir::Name);
            for (const QFileInfo &entry : links) {
                if (++inspected > 512) break;
                const QString name = entry.completeBaseName().toLower();
                if (!name.contains("terminal") && !name.contains("powershell") && !name.contains("8bitshell") && name != "command prompt") continue;
                const QString target = shortcutTarget(entry.absoluteFilePath());
                // A display name is not an executable identity: installers add
                // links such as "Uninstall 8bitShell" targeting msiexec.exe.
                if (!target.isEmpty() && knownTerminalTarget(target))
                    append(result, adapter(target, entry.completeBaseName()));
            }
        }
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
#else
// Desktop entries are read as data. Exec is tokenized according to the desktop
// entry quoting rules and is never handed to a shell or evaluated.
QStringList desktopArguments(const QString &value, bool *ok)
{
    QStringList arguments;
    QString token;
    bool quoted = false;
    bool escaped = false;
    bool started = false;
    *ok = false;
    for (QChar ch : value) {
        if (escaped) { token += ch; escaped = false; started = true; }
        else if (ch == '\\') { escaped = true; started = true; }
        else if (ch == '"') { quoted = !quoted; started = true; }
        else if (ch.isSpace() && !quoted) {
            if (started) { arguments << token; token.clear(); started = false; }
        } else { token += ch; started = true; }
    }
    if (quoted || escaped) return {};
    if (started) arguments << token;
    *ok = !arguments.isEmpty();
    return arguments;
}

QString desktopString(const QString &value)
{
    QString result;
    for (int index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1 >= value.size()) { result += value[index]; continue; }
        const QChar next = value[index + 1];
        if (next == 's') result += ' ';
        else if (next == 'n') result += '\n';
        else if (next == 't') result += '\t';
        else if (next == 'r') result += '\r';
        else if (next == '\\') result += '\\';
        else { result += '\\'; continue; }
        ++index;
    }
    return result;
}

Terminal desktopTerminal(const QFileInfo &entry)
{
    QFile file(entry.absoluteFilePath());
    if (file.size() > 262144 || !file.open(QIODevice::ReadOnly)) return {};
    QHash<QString, QString> values;
    bool mainGroup = false;
    const auto lines = QString::fromUtf8(file.readAll()).split('\n');
    for (QString line : lines) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith('[')) { mainGroup = line == "[Desktop Entry]"; continue; }
        if (!mainGroup || line.startsWith('#')) continue;
        const qsizetype equal = line.indexOf('=');
        if (equal > 0) values.insert(line.left(equal), desktopString(line.mid(equal + 1)));
    }
    if (values.value("Type") != "Application" || values.value("Hidden") == "true"
        || values.value("NoDisplay") == "true" || values.value("Terminal") == "true") return {};
    if (!values.value("Categories").split(';').contains("TerminalEmulator")) return {};
    if (!values.value("TryExec").isEmpty() && executablePath(values.value("TryExec"), true).isEmpty()) return {};
    bool parsed = false;
    QStringList args = desktopArguments(values.value("Exec"), &parsed);
    if (!parsed) return {};
    const QString program = executablePath(args.takeFirst(), true);
    if (program.isEmpty()) return {};
    const QString base = QFileInfo(program).fileName().toLower();
    const QStringList wrappers = {"sh", "bash", "dash", "zsh", "fish", "env", "python", "python3", "perl", "ruby", "node", "flatpak", "snap"};
    if (wrappers.contains(base)) return {};
    Terminal terminal = adapter(program, values.value("Name"));
    if (!terminal.arguments.isEmpty() || !terminal.id.startsWith("terminal-")) return terminal;
    // A desktop registration establishes an application identity, not consent
    // to run its optional command payload (for example, terminal -e sh -c ...).
    // Unknown terminals inherit cwd without importing Exec options. Explicit
    // profile flags and other arguments belong in the Custom configuration.
    terminal.id = "desktop:" + entry.fileName();
    terminal.arguments.clear();
    return terminal;
}

QList<Terminal> discoverLinux()
{
    QList<Terminal> result;
    const QStringList names = {"x-terminal-emulator", "gnome-terminal", "kgx", "konsole", "xfce4-terminal", "mate-terminal",
                               "lxterminal", "tilix", "kitty", "alacritty", "wezterm", "foot", "footclient", "urxvt", "xterm", "8bitShell", "8bitshell", "8bitShell"};
    for (const QString &name : names) {
        QString program = executablePath(name, true);
        if (program.isEmpty()) continue;
        if (name == "x-terminal-emulator") {
            const QString canonical = QFileInfo(program).canonicalFilePath();
            if (!canonical.isEmpty()) program = canonical;
        }
        append(result, adapter(program));
    }
    QStringList roots = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    roots.removeDuplicates();
    QSet<QString> desktopIds;
    int inspected = 0;
    for (const QString &root : roots) {
        const auto entries = QDir(QDir(root).filePath("applications")).entryInfoList({"*.desktop"}, QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (++inspected > 2048) break;
            if (desktopIds.contains(entry.fileName())) continue;
            desktopIds.insert(entry.fileName());
            auto terminal = desktopTerminal(entry);
            if (!terminal.program.isEmpty()) append(result, terminal);
        }
        if (inspected > 2048) break;
    }
    return result;
}
#endif

QProcessEnvironment terminalEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // A portable Qt application may set plugin paths to its own deployment.
    // Do not force those plugins into another independently installed Qt app.
    const QString app = QDir::cleanPath(QCoreApplication::applicationDirPath());
    for (const QString &key : {QString("QT_PLUGIN_PATH"), QString("QT_QPA_PLATFORM_PLUGIN_PATH")}) {
        if (!environment.contains(key)) continue;
        QStringList kept;
        bool removed = false;
        for (const QString &entry : environment.value(key).split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
            const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(entry));
#ifdef Q_OS_WIN
            constexpr auto sensitivity = Qt::CaseInsensitive;
#else
            constexpr auto sensitivity = Qt::CaseSensitive;
#endif
            if (clean.compare(app, sensitivity) == 0 || clean.startsWith(app + '/', sensitivity)) removed = true;
            else kept << entry;
        }
        if (removed) {
            if (kept.isEmpty()) environment.remove(key);
            else environment.insert(key, kept.join(QDir::listSeparator()));
        }
    }
    return environment;
}
}

QList<Terminal> discover(bool refresh)
{
    static QMutex mutex;
    static QElapsedTimer age;
    static QList<Terminal> cached;
    const QMutexLocker locker(&mutex);
    if (!refresh && age.isValid() && age.elapsed() < 30000) return cached;
#ifdef Q_OS_WIN
    cached = discoverWindows();
#else
    cached = discoverLinux();
#endif
    age.restart();
    return cached;
}

Settings readSettings(const QSettings *settings)
{
    Settings value;
    if (!settings) return value;
    value.terminalId = settings->value(settingsPrefix + "Id", "auto").toString();
    if (value.terminalId.isEmpty()) value.terminalId = "auto";
    value.customName = settings->value(settingsPrefix + "CustomName").toString();
    value.customProgram = settings->value(settingsPrefix + "CustomProgram").toString();
    value.customArguments = settings->value(settingsPrefix + "CustomArguments").toStringList();
    value.customNewConsole = settings->value(settingsPrefix + "CustomNewConsole", false).toBool();
    return value;
}

void writeSettings(QSettings *settings, const Settings &value)
{
    if (!settings) return;
    settings->setValue(settingsPrefix + "Id", value.terminalId.isEmpty() ? "auto" : value.terminalId);
    settings->setValue(settingsPrefix + "CustomName", value.customName);
    settings->setValue(settingsPrefix + "CustomProgram", value.customProgram);
    settings->setValue(settingsPrefix + "CustomArguments", value.customArguments);
    settings->setValue(settingsPrefix + "CustomNewConsole", value.customNewConsole);
}

Terminal terminalForSettings(const Settings &settings, const QList<Terminal> &available)
{
    if (settings.terminalId == "custom") {
        Terminal terminal{"custom", settings.customName.trimmed().isEmpty() ? QObject::tr("Custom terminal") : settings.customName,
                          settings.customProgram, settings.customArguments, Kind::Custom};
        terminal.newConsole = settings.customNewConsole;
        return terminal;
    }
    if ((settings.terminalId.isEmpty() || settings.terminalId == "auto") && !available.isEmpty()) return available.first();
    for (const Terminal &terminal : available) if (terminal.id == settings.terminalId) return terminal;
    return {settings.terminalId, QObject::tr("Unavailable terminal"), {}, {}, Kind::Terminal};
}

LaunchSpec buildLaunch(const Terminal &terminal, const QString &folder)
{
    LaunchSpec spec;
    const QFileInfo directory(folder);
    if (folder.isEmpty() || folder.contains(QChar::Null) || !directory.exists() || !directory.isDir()) {
        spec.error = QObject::tr("The selected folder no longer exists or is not a folder.");
        return spec;
    }
    spec.workingDirectory = QDir::cleanPath(directory.absoluteFilePath());
    spec.program = executablePath(terminal.program);
    if (spec.program.isEmpty()) {
        spec.error = QObject::tr("The selected terminal executable was not found. Choose an installed application or an executable path in Preferences > Terminal. Batch files and command text are not supported.");
        return spec;
    }
    spec.arguments = terminal.arguments;
    if (terminal.kind == Kind::WindowsTerminal) {
        // Windows Terminal parses semicolons as command separators even in
        // quoted arguments. Keep the untrusted directory out of that grammar.
        spec.arguments = {"-w", "new", "new-tab", "-d", "."};
    } else {
        const QString nativeFolder = QDir::toNativeSeparators(spec.workingDirectory);
        for (QString &argument : spec.arguments) {
            if (argument.contains(QChar::Null)) {
                spec.error = QObject::tr("Terminal arguments cannot contain a null character.");
                return spec;
            }
            argument.replace("{folder}", nativeFolder);
        }
    }
#ifdef Q_OS_WIN
    spec.newConsole = terminal.kind == Kind::WindowsConsole || terminal.newConsole;
    if (QFileInfo(spec.program).fileName().compare("cmd.exe", Qt::CaseInsensitive) == 0
        && spec.workingDirectory.startsWith("//"))
        spec.error = QObject::tr("Command Prompt cannot use a network UNC folder as its working directory. Choose PowerShell or another terminal.");
#endif
    return spec;
}

LaunchResult launch(const LaunchSpec &spec)
{
    if (!spec.valid()) return {false, spec.error.isEmpty() ? QObject::tr("The terminal launch configuration is incomplete.") : spec.error, 0};
    if (spec.workingDirectory.contains(QChar::Null))
        return {false, QObject::tr("The working directory is invalid."), 0};
    for (const QString &argument : spec.arguments)
        if (argument.contains(QChar::Null)) return {false, QObject::tr("Terminal arguments cannot contain a null character."), 0};
    // Validate again immediately before creation: the menu may have been open
    // while an executable or selected directory was removed.
    if (!QFileInfo(spec.workingDirectory).isDir()) return {false, QObject::tr("The selected folder no longer exists."), 0};
    const QString program = executablePath(spec.program);
    if (program.isEmpty()) return {false, QObject::tr("The selected terminal executable no longer exists."), 0};
    QProcess process;
    process.setProgram(program);
    process.setArguments(spec.arguments);
    process.setWorkingDirectory(spec.workingDirectory);
    process.setProcessEnvironment(terminalEnvironment());
#ifdef Q_OS_WIN
    if (spec.newConsole) process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags &= ~(DETACHED_PROCESS | CREATE_NO_WINDOW);
        arguments->flags |= CREATE_NEW_CONSOLE;
        arguments->inheritHandles = false;
        arguments->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_SHOWNORMAL;
    });
#endif
    qint64 pid = 0;
    if (!process.startDetached(&pid))
        return {false, QObject::tr("Could not start the terminal: %1").arg(process.errorString()), 0};
    return {true, {}, pid};
}

LaunchResult open(const Settings &settings, const QString &folder)
{
    return launch(buildLaunch(terminalForSettings(settings, discover()), folder));
}

QStringList argumentsFromText(const QString &text)
{
    QStringList result;
    for (QString line : text.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty()) result << line;
    }
    return result;
}

QString argumentsToText(const QStringList &arguments)
{
    return arguments.join('\n');
}
}
