// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExplorerFileOperations.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace Explorer {
namespace {
#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif

QString absolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString &a, const QString &b)
{
    return a.compare(b, pathCase) == 0;
}

bool inside(const QString &path, const QString &parent)
{
    const QString prefix = parent.endsWith('/') ? parent : parent + '/';
    return samePath(path, parent) || path.startsWith(prefix, pathCase);
}

bool link(const QFileInfo &info)
{
    return info.isSymLink() || info.isJunction();
}

OperationResult failed(const QString &message)
{
    return {false, {}, message, false};
}

OperationResult success(const QString &path)
{
    return {true, path, {}, false};
}

OperationResult copyEntry(const QString &source, const QString &target,
                          const std::function<bool()> &cancelled)
{
    if (cancelled && cancelled())
        return {false, {}, {}, true};
    const QFileInfo info(source);
    if (link(info))
        return failed(QObject::tr("Links and junctions cannot be copied. No files were replaced."));
    if (info.isDir()) {
        if (!QDir().mkdir(target))
            return failed(QObject::tr("Could not create the destination folder: %1").arg(target));
        QDir directory(source);
        if (!directory.isReadable())
            return failed(QObject::tr("The folder cannot be read: %1").arg(source));
        const auto entries = directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                     | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            auto result = copyEntry(entry.absoluteFilePath(), QDir(target).filePath(entry.fileName()), cancelled);
            if (!result.ok)
                return result;
        }
    } else if (info.isFile()) {
        QFile input(source);
        QFile output(target);
        if (!input.open(QIODevice::ReadOnly))
            return failed(QObject::tr("Could not read %1: %2").arg(source, input.errorString()));
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return failed(QObject::tr("Could not create %1: %2").arg(target, output.errorString()));
        const qint64 initialSize = input.size();
        qint64 copied = 0;
        while (!input.atEnd()) {
            if (cancelled && cancelled())
                return {false, {}, {}, true};
            const QByteArray block = input.read(1024 * 1024);
            if (input.error() != QFileDevice::NoError || (block.isEmpty() && !input.atEnd()))
                return failed(QObject::tr("Could not finish reading %1: %2").arg(source, input.errorString()));
            if (output.write(block) != block.size())
                return failed(QObject::tr("Could not finish writing %1: %2").arg(target, output.errorString()));
            copied += block.size();
        }
        if (!output.flush())
            return failed(QObject::tr("Could not finish writing %1: %2").arg(target, output.errorString()));
        // A changing source is not a consistent snapshot. Keep the original and
        // discard the staging copy rather than silently publish a truncated file.
        const QFileInfo after(source);
        if (copied != initialSize || after.size() != initialSize
            || after.lastModified() != info.lastModified() || link(after))
            return failed(QObject::tr("The source changed while it was being copied: %1").arg(source));
        output.close();
        if (!QFile::setPermissions(target, info.permissions()))
            return failed(QObject::tr("Could not preserve file permissions: %1").arg(source));
    } else {
        return failed(QObject::tr("This item is not a regular file or folder: %1").arg(source));
    }
    return success(target);
}
}

FileOperations::FileOperations(const QString &workspace)
{
    const QFileInfo info(workspace);
    if (!workspace.isEmpty() && info.isDir())
        root = QDir::cleanPath(info.canonicalFilePath());
}

bool FileOperations::validName(const QString &name, QString *error)
{
    QString reason;
    static const QRegularExpression invalid(QStringLiteral("[\\x00-\\x1f<>:\"/\\\\|?*]"));
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty() || name.trimmed().isEmpty() || name == "." || name == "..")
        reason = QObject::tr("Enter a file or folder name.");
    else if (invalid.match(name).hasMatch())
        reason = QObject::tr("Use a name without path separators or these characters: < > : \" | ? *");
    else if (name.endsWith('.') || name.endsWith(' '))
        reason = QObject::tr("Names cannot end with a dot or a space.");
    else if (reserved.match(name).hasMatch())
        reason = QObject::tr("That name is reserved by Windows. Choose another name.");
    if (error)
        *error = reason;
    return reason.isEmpty();
}

QString FileOperations::validateExisting(const QString &path, bool allowRoot) const
{
    if (root.isEmpty())
        return QObject::tr("Open a workspace folder first.");
    const QString full = absolute(path);
    if (!inside(full, root) || (!allowRoot && samePath(full, root)))
        return QObject::tr("Choose an item inside the open workspace folder.");
    const QFileInfo info(full);
    if (!info.exists() && !info.isSymLink())
        return QObject::tr("The item no longer exists: %1").arg(full);
    QString current = full;
    while (true) {
        if (link(QFileInfo(current)))
            return QObject::tr("This operation cannot follow a symbolic link or junction.");
        if (samePath(current, root))
            break;
        const QString parent = QFileInfo(current).absolutePath();
        if (samePath(parent, current) || !inside(parent, root))
            return QObject::tr("The item is outside the open workspace folder.");
        current = parent;
    }
    if (!inside(QDir::cleanPath(info.canonicalFilePath()), root))
        return QObject::tr("The item resolves outside the open workspace folder.");
    return {};
}

QString FileOperations::validateTarget(const QString &parent, const QString &name) const
{
    QString reason;
    if (!validName(name, &reason))
        return reason;
    reason = validateExisting(parent, true);
    if (!reason.isEmpty())
        return reason;
    if (!QFileInfo(parent).isDir())
        return QObject::tr("Choose a destination folder.");
    const QFileInfo target(QDir(parent).filePath(name));
    if (target.exists() || target.isSymLink() || target.isJunction())
        return QObject::tr("An item named \"%1\" already exists. Nothing was replaced.").arg(name);
    return {};
}

OperationResult FileOperations::createFile(const QString &parent, const QString &name) const
{
    const QString reason = validateTarget(parent, name);
    if (!reason.isEmpty())
        return failed(reason);
    const QString target = absolute(QDir(parent).filePath(name));
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return failed(QObject::tr("Could not create the file: %1").arg(file.errorString()));
    return success(target);
}

OperationResult FileOperations::createDirectory(const QString &parent, const QString &name) const
{
    const QString reason = validateTarget(parent, name);
    if (!reason.isEmpty())
        return failed(reason);
    const QString target = absolute(QDir(parent).filePath(name));
    if (!QDir().mkdir(target))
        return failed(QObject::tr("Could not create the folder. Check the name and folder permissions."));
    return success(target);
}

OperationResult FileOperations::rename(const QString &source, const QString &name) const
{
    QString reason;
    if (!validName(name, &reason))
        return failed(reason);
    const QString target = QDir(QFileInfo(source).absolutePath()).filePath(name);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const QString from = absolute(source);
    const QString to = absolute(target);
    const bool caseOnly = from.compare(to, Qt::CaseInsensitive) == 0 && from != to;
    bool sameFile = caseOnly;
#ifdef Q_OS_MACOS
    // A case-sensitive volume can store both names. Only treat this as a
    // case-only rename when both paths are the same existing file.
    const QString fromCanonical = QFileInfo(from).canonicalFilePath();
    sameFile = caseOnly && !fromCanonical.isEmpty()
        && QFileInfo(to).canonicalFilePath().compare(fromCanonical, Qt::CaseInsensitive) == 0;
#endif
    if (sameFile) {
        reason = validateExisting(source, false);
        if (!reason.isEmpty()) return failed(reason);
        // A direct case-only rename is rejected on Windows and on
        // case-insensitive macOS volumes because the destination "exists".
        // Reserve a unique sibling container first.
        QTemporaryDir staging(QDir(QFileInfo(from).absolutePath()).filePath(".8bitpad-rename-XXXXXX"));
        if (!staging.isValid())
            return failed(QObject::tr("Could not prepare the rename. The source was kept."));
        const QString staged = QDir(staging.path()).filePath(QFileInfo(from).fileName());
        if (!QDir().rename(from, staged))
            return failed(QObject::tr("Could not rename the item. The source was kept."));
        if (QDir().rename(staged, to))
            return success(to);
        if (QDir().rename(staged, from))
            return failed(QObject::tr("Could not use the new name. The original name was restored."));
        // A concurrent creator may now occupy both names. Never remove the
        // original data when rollback is impossible; report its recovery path.
        staging.setAutoRemove(false);
        return {false, staged,
            QObject::tr("The rename could not finish and the original name is now occupied. Your item was kept safely at:\n%1").arg(staged), false};
    }
#endif
    return moveTo(source, target);
}

OperationResult FileOperations::move(const QString &source, const QString &destinationDirectory) const
{
    return moveTo(source, QDir(destinationDirectory).filePath(QFileInfo(source).fileName()));
}

OperationResult FileOperations::moveTo(const QString &source, const QString &destination) const
{
    QString reason = validateExisting(source, false);
    if (!reason.isEmpty())
        return failed(reason);
    const QString from = absolute(source);
    const QString to = absolute(destination);
    if (from == to)
        return success(to);
    // Validate the original supplied name, before path normalization can remove
    // traversal segments. rename() separately checks it for the same reason.
    reason = validateTarget(QFileInfo(destination).absolutePath(), QFileInfo(destination).fileName());
    if (!reason.isEmpty())
        return failed(reason);
    if (QFileInfo(from).isDir() && inside(to, from))
        return failed(QObject::tr("A folder cannot be moved into itself or one of its children."));
    if (!QDir().rename(from, to))
        return failed(QObject::tr("Could not move the item. Check permissions and that both folders are on the same drive. The source was kept."));
    return success(to);
}

OperationResult FileOperations::copy(const QString &source, const QString &destinationDirectory,
                                    const std::function<bool()> &cancelled) const
{
    QString reason = validateExisting(source, false);
    if (!reason.isEmpty())
        return failed(reason);
    const QFileInfo info(source);
    reason = validateTarget(destinationDirectory, info.fileName());
    if (!reason.isEmpty())
        return failed(reason);
    const QString target = absolute(QDir(destinationDirectory).filePath(info.fileName()));
    if (info.isDir() && inside(target, absolute(source)))
        return failed(QObject::tr("A folder cannot be copied into itself or one of its children."));
    QTemporaryDir staging(QDir(destinationDirectory).filePath(QStringLiteral(".8bitpad-copy-XXXXXX")));
    if (!staging.isValid())
        return failed(QObject::tr("Could not prepare a temporary copy in the destination folder."));
    const QString staged = QDir(staging.path()).filePath(info.fileName());
    auto result = copyEntry(absolute(source), staged, cancelled);
    if (!result.ok)
        return result;
    if (cancelled && cancelled())
        return {false, {}, {}, true};
    reason = validateTarget(destinationDirectory, info.fileName());
    if (!reason.isEmpty())
        return failed(reason);
    if (!QDir().rename(staged, target))
        return failed(QObject::tr("Could not finish the copy. The source was kept and nothing was replaced."));
    return success(target);
}

OperationResult FileOperations::trash(const QString &source) const
{
    const QString reason = validateExisting(source, false);
    if (!reason.isEmpty())
        return failed(reason);
    QFile item(absolute(source));
    if (!item.moveToTrash())
        return failed(QObject::tr("Could not move this item to the Recycle Bin / Trash: %1\nIt has not been permanently deleted.").arg(item.errorString()));
    return success(absolute(source));
}

}
