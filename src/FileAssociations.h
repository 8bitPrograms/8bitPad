// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class QSettings;

namespace FileAssociations {

struct Group { QString id; QString title; QStringList extensions; };
struct ParsedExtensions {
    QStringList extensions;
    QString error;
    bool valid() const { return error.isEmpty(); }
};
struct BatchResult {
    QStringList registeredExtensions;
    QStringList defaultMimeTypes;
    QStringList errors;
    bool defaultsUiOpened = false;
    bool ok() const { return errors.isEmpty(); }
};

QList<Group> catalog();
ParsedExtensions parseExtensions(const QString &text);
QStringList registeredExtensions();
BatchResult apply(const QStringList &extensions, bool requestDefaults = false);
BatchResult removeOwnedRegistration();
bool contextMenuInstalled();
BatchResult setContextMenuEnabled(bool enabled);

// Platform operations are injectable so tests use a temporary INI registry,
// temporary XDG directories and fake command results, never the user's defaults.
enum class Platform { Windows, Linux };
struct CommandResult {
    bool started = false;
    int exitCode = -1;
    QString standardOutput;
    QString error;
};
struct Services {
    Platform platform = Platform::Linux;
    QString executable;
    QSettings *registry = nullptr; // HKCU root in production; temporary INI in tests.
    QString dataHome;
    QString configHome;
    std::function<QString(const QString &)> findExecutable;
    std::function<CommandResult(const QString &, const QStringList &)> run;
    std::function<bool(const QUrl &)> openDefaults;
    std::function<void()> associationsChanged;
};
class Backend
{
public:
    explicit Backend(Services services);
    QStringList registeredExtensions() const;
    BatchResult apply(const QStringList &extensions, bool requestDefaults = false);
    BatchResult removeOwnedRegistration();
    bool contextMenuInstalled() const;
    BatchResult setContextMenuEnabled(bool enabled);
private:
    Services services;
};

}
