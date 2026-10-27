// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

enum class GitPathState { Modified, Added, Untracked, Deleted };

struct GitSnapshot
{
    QString branch;
    int changed = 0;
    int untracked = 0;
    QHash<QString, GitPathState> paths;

    QString label() const;
    QString toolTip() const;
};

QString normalizePath(const QString &path);
QString findGitRoot(const QString &start);
GitSnapshot parseGitStatus(const QByteArray &porcelain, const QString &repoRoot, const QString &workspaceRoot);

// One debounced `git status` at a time. No filesystem watcher.
class GitMonitor final : public QObject
{
    Q_OBJECT
public:
    explicit GitMonitor(QObject *parent = nullptr);
    ~GitMonitor() override;
    void watch(const QString &folder);
    void schedule();

signals:
    void updated(const GitSnapshot &snapshot);

private:
    void start();
    void finish(int exitCode, int status);

    class QProcess *process = nullptr;
    class QTimer *timer = nullptr;
    QString workspace;
    QString repo;
    QString gitProgram;
    bool gitAvailable = true;
    bool dirty = false;
    int generation = 0;
};
