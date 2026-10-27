#pragma once

#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

namespace FolderSearch {
using Cancellation = std::shared_ptr<std::atomic_bool>;

struct Options {
    QString directory;
    QString pattern;
    QStringList masks {QStringLiteral("*")};
    bool recursive = true;
    bool regex = false;
    bool caseSensitive = false;
    bool wholeWord = false;
    qint64 maxFileBytes = 32 * 1024 * 1024;
    int maxMatches = 5000;
    int maxFiles = 10000;
};

enum class Encoding { Utf8, Utf8Bom, Utf16LE, Utf16BE };

struct Match {
    qsizetype start = 0;
    qsizetype length = 0;
    int line = 1;
    int column = 1;
    QString preview;
    QStringList captures;
};

struct FileResult {
    QString path;
    QByteArray digest;
    Encoding encoding = Encoding::Utf8;
    QVector<Match> matches;
};

struct Result {
    Options options;
    QVector<FileResult> files;
    int scanned = 0;
    int skipped = 0;
    int matches = 0;
    bool cancelled = false;
    bool truncated = false;
    QString error;
};

struct ReplaceResult {
    int files = 0;
    int matches = 0;
    int skipped = 0;
    bool cancelled = false;
    QStringList backups;
    QStringList errors;
};

Result scan(const Options &options, const Cancellation &cancel);
ReplaceResult replace(const Result &preview, const QString &replacement,
                      const QStringList &openFiles, const Cancellation &cancel);
}
