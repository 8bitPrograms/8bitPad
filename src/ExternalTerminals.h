// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class QSettings;

namespace ExternalTerminals {

enum class Kind { Terminal, WindowsTerminal, WindowsConsole, Custom };

struct Terminal
{
    QString id;
    QString name;
    QString program;
    QStringList arguments;
    Kind kind = Kind::Terminal;
    bool newConsole = false;
};

struct Settings
{
    QString terminalId = QStringLiteral("auto");
    QString customName;
    QString customProgram;
    QStringList customArguments;
    bool customNewConsole = false;
};

struct LaunchSpec
{
    QString program;
    QStringList arguments;
    QString workingDirectory;
    bool newConsole = false;
    QString error;
    bool valid() const { return error.isEmpty() && !program.isEmpty() && !workingDirectory.isEmpty(); }
};

struct LaunchResult
{
    bool ok = false;
    QString error;
    qint64 processId = 0;
};

// Read-only discovery: PATH, bounded application registrations and known paths.
// It never launches programs. Results are cached for thirty seconds.
QList<Terminal> discover(bool refresh = false);
Settings readSettings(const QSettings *settings);
void writeSettings(QSettings *settings, const Settings &value);
Terminal terminalForSettings(const Settings &settings, const QList<Terminal> &available);
LaunchSpec buildLaunch(const Terminal &terminal, const QString &folder);
LaunchResult launch(const LaunchSpec &spec);
LaunchResult open(const Settings &settings, const QString &folder);

// Each nonempty line is one literal argv item; no shell parsing or evaluation.
QStringList argumentsFromText(const QString &text);
QString argumentsToText(const QStringList &arguments);

}
