// SPDX-License-Identifier: GPL-3.0-or-later
#include "GitStatus.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

QString normalizePath(const QString &path)
{
    QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    clean = clean.toLower();
#endif
    return clean;
}

QString findGitRoot(const QString &start)
{
    if (start.isEmpty())
        return {};
    QDir dir(start);
    if (!dir.exists())
        return {};
    for (int depth = 0; depth < 32; ++depth) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git"))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return {};
}

namespace {
GitPathState classifyStatus(char index, char work)
{
    if (index == '?' || work == '?')
        return GitPathState::Untracked;
    if (index == 'A' || work == 'A')
        return GitPathState::Added;
    if (index == 'D' || work == 'D')
        return GitPathState::Deleted;
    return GitPathState::Modified;
}

void markParents(QHash<QString, GitPathState> &paths, const QString &file, const QString &workspace)
{
    const QString root = normalizePath(workspace);
    QString parent = file;
    while (true) {
        const int slash = parent.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0)
            break;
        parent = parent.left(slash);
        if (!parent.startsWith(root) || parent == root)
            break;
        if (!paths.contains(parent))
            paths.insert(parent, GitPathState::Modified);
    }
}
}

QString GitSnapshot::label() const
{
    if (branch.isEmpty())
        return {};
    if (changed > 0 || untracked > 0)
        return branch + QStringLiteral(" •");
    return branch;
}

QString GitSnapshot::toolTip() const
{
    if (branch.isEmpty())
        return {};
    return QStringLiteral("Git %1\n%2 changed, %3 untracked")
        .arg(branch)
        .arg(changed)
        .arg(untracked);
}

GitSnapshot parseGitStatus(const QByteArray &porcelain, const QString &repoRoot, const QString &workspaceRoot)
{
    GitSnapshot snapshot;
    const QString workspace = workspaceRoot.isEmpty() ? repoRoot : workspaceRoot;
    int cursor = 0;
    auto nextRecord = [&porcelain, &cursor]() {
        if (cursor >= porcelain.size())
            return QByteArray();
        const int end = porcelain.indexOf('\0', cursor);
        const int stop = end < 0 ? porcelain.size() : end;
        const QByteArray record = porcelain.mid(cursor, stop - cursor);
        cursor = stop + (end < 0 ? 0 : 1);
        return record;
    };

    while (cursor < porcelain.size()) {
        const QByteArray record = nextRecord();
        if (record.isEmpty())
            continue;
        if (record.startsWith("##")) {
            QString header = QString::fromUtf8(record.mid(2)).trimmed();
            const int dots = header.indexOf(QStringLiteral("..."));
            if (dots >= 0)
                header = header.left(dots);
            snapshot.branch = header.trimmed();
            continue;
        }
        if (record.size() < 4 || record.at(2) != ' ')
            continue;
        const char index = record.at(0);
        const char work = record.at(1);
        const GitPathState state = classifyStatus(index, work);
        const bool rename = index == 'R' || index == 'C' || work == 'R' || work == 'C';
        const QString relative = QString::fromUtf8(record.mid(3));
        const QString absolute = normalizePath(QDir(repoRoot).filePath(relative));
        snapshot.paths.insert(absolute, state);
        markParents(snapshot.paths, absolute, workspace);
        if (rename) {
            const QByteArray original = nextRecord();
            if (!original.isEmpty()) {
                const QString previous = normalizePath(QDir(repoRoot).filePath(QString::fromUtf8(original)));
                snapshot.paths.insert(previous, GitPathState::Deleted);
                markParents(snapshot.paths, previous, workspace);
            }
        }
        if (state == GitPathState::Untracked)
            ++snapshot.untracked;
        else
            ++snapshot.changed;
    }
    return snapshot;
}

GitMonitor::GitMonitor(QObject *parent)
    : QObject(parent), process(new QProcess(this)), timer(new QTimer(this))
{
    timer->setSingleShot(true);
    gitProgram = QStandardPaths::findExecutable(QStringLiteral("git"));
    gitAvailable = !gitProgram.isEmpty();
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(timer, &QTimer::timeout, this, [this] {
        if (process->state() != QProcess::NotRunning)
            return;
        if (dirty)
            start();
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            gitAvailable = false;
            emit updated(GitSnapshot{});
        }
    });
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        finish(exitCode, static_cast<int>(status));
    });
}

GitMonitor::~GitMonitor()
{
    ++generation;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning)
        process->kill();
}

void GitMonitor::watch(const QString &folder)
{
    workspace = folder;
    repo = findGitRoot(folder);
    if (repo.isEmpty() || !gitAvailable) {
        emit updated(GitSnapshot{});
        return;
    }
    schedule();
}

void GitMonitor::schedule()
{
    if (repo.isEmpty() || !gitAvailable)
        return;
    dirty = true;
    timer->start(300);
}

void GitMonitor::start()
{
    if (repo.isEmpty() || !gitAvailable || process->state() != QProcess::NotRunning)
        return;
    dirty = false;
    const int token = ++generation;
    process->setProperty("token", token);
    process->setWorkingDirectory(repo);
    process->start(gitProgram, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-z"), QStringLiteral("-b")}, QIODevice::ReadOnly);
}

void GitMonitor::finish(int exitCode, int status)
{
    const int token = process->property("token").toInt();
    if (token != generation)
        return;
    if (status == QProcess::NormalExit && exitCode == 0) {
        const QByteArray output = process->readAllStandardOutput().left(512 * 1024);
        emit updated(parseGitStatus(output, repo, workspace));
    }
    if (dirty)
        timer->start(50);
}
