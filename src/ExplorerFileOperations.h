// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <functional>

namespace Explorer {

struct OperationResult
{
    bool ok = false;
    QString path;
    QString error;
    bool cancelled = false;
};

// All mutating paths are confined to an explicitly opened workspace. Links are
// intentionally not followed: the explorer must not mutate an unseen target.
class FileOperations
{
public:
    explicit FileOperations(const QString &workspace);
    static bool validName(const QString &name, QString *error = nullptr);
    OperationResult createFile(const QString &parent, const QString &name) const;
    OperationResult createDirectory(const QString &parent, const QString &name) const;
    OperationResult rename(const QString &source, const QString &name) const;
    OperationResult move(const QString &source, const QString &destinationDirectory) const;
    OperationResult copy(const QString &source, const QString &destinationDirectory,
                         const std::function<bool()> &cancelled = {}) const;
    OperationResult trash(const QString &source) const;

private:
    QString validateExisting(const QString &path, bool allowRoot) const;
    QString validateTarget(const QString &parent, const QString &name) const;
    OperationResult moveTo(const QString &source, const QString &destination) const;
    QString root;
};

}
