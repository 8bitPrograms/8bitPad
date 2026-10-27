#include "FolderSearch.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringConverter>
#include <QUuid>

namespace FolderSearch {
namespace {
bool cancelled(const Cancellation &cancel) { return cancel && cancel->load(); }

QString normalizedPath(const QString &path)
{
    const QFileInfo info(path);
    QString value = info.canonicalFilePath();
    if (value.isEmpty()) value = info.absoluteFilePath();
#ifdef Q_OS_WIN
    value = value.toCaseFolded();
#endif
    return value;
}

bool read(const QString &path, qint64 limit, QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
        return false;
    bytes = file.read(limit + 1);
    return file.error() == QFileDevice::NoError && bytes.size() <= limit && file.atEnd();
}

bool decode(const QByteArray &bytes, QString &text, Encoding &encoding)
{
    qsizetype offset = 0;
    auto codec = QStringConverter::Utf8;
    encoding = Encoding::Utf8;
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) {
        offset = 3;
        encoding = Encoding::Utf8Bom;
    }
    else if (bytes.startsWith(QByteArray::fromHex("fffe"))) {
        offset = 2;
        codec = QStringConverter::Utf16LE;
        encoding = Encoding::Utf16LE;
    }
    else if (bytes.startsWith(QByteArray::fromHex("feff"))) {
        offset = 2;
        codec = QStringConverter::Utf16BE;
        encoding = Encoding::Utf16BE;
    }
    // Qt's stateless UTF-16 decoder does not reject an odd trailing byte on
    // every supported version. Never offer a replacement for truncated input.
    if (codec != QStringConverter::Utf8 && (bytes.size() - offset) % 2 != 0)
        return false;
    QStringDecoder decoder(codec, QStringConverter::Flag::Stateless);
    text = decoder(QByteArrayView(bytes).sliced(offset));
    return !decoder.hasError() && text.isValidUtf16() && !text.contains(QChar::Null);
}

QByteArray encode(const QString &text, Encoding encoding)
{
    if (encoding == Encoding::Utf8) return text.toUtf8();
    if (encoding == Encoding::Utf8Bom) return QByteArray::fromHex("efbbbf") + text.toUtf8();
    QStringEncoder encoder(encoding == Encoding::Utf16LE ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
    return QByteArray::fromHex(encoding == Encoding::Utf16LE ? "fffe" : "feff") + QByteArray(encoder(text));
}

QString replacementFor(const QString &replacement, const Match &match, bool regex)
{
    if (!regex) return replacement;
    QString expanded;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        if (replacement[i] == QLatin1Char('\\') && i + 1 < replacement.size()) {
            const QChar next = replacement[++i];
            if (next.isDigit()) {
                int capture = next.digitValue();
                if (i + 1 < replacement.size() && replacement[i + 1].isDigit()) {
                    const int pair = capture * 10 + replacement[i + 1].digitValue();
                    if (pair < match.captures.size()) { capture = pair; ++i; }
                }
                expanded += match.captures.value(capture);
            }
            else if (next == QLatin1Char('n')) expanded += QLatin1Char('\n');
            else if (next == QLatin1Char('r')) expanded += QLatin1Char('\r');
            else if (next == QLatin1Char('t')) expanded += QLatin1Char('\t');
            else if (next == QLatin1Char('\\')) expanded += next;
            else { expanded += QLatin1Char('\\'); expanded += next; }
        }
        else expanded += replacement[i];
    }
    return expanded;
}
}

Result scan(const Options &options, const Cancellation &cancel)
{
    Result result;
    result.options = options;
    if (!QFileInfo(options.directory).isDir() || options.pattern.isEmpty()) {
        result.error = QStringLiteral("Choose an existing folder and enter search text.");
        return result;
    }
    QString expression = options.regex ? options.pattern : QRegularExpression::escape(options.pattern);
    if (options.wholeWord) expression = QStringLiteral("(?<![\\p{L}\\p{N}_])(?:%1)(?![\\p{L}\\p{N}_])").arg(expression);
    auto flags = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::MultilineOption;
    if (!options.caseSensitive) flags |= QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression regex(expression, flags);
    if (!regex.isValid()) { result.error = regex.errorString(); return result; }
    qsizetype captureCharacters = 0;

    QDirIterator files(options.directory, options.masks, QDir::Files | QDir::NoSymLinks,
                       options.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (files.hasNext()) {
        if (cancelled(cancel)) { result.cancelled = true; break; }
        if (result.scanned >= options.maxFiles || result.matches >= options.maxMatches) { result.truncated = true; break; }
        const QString path = files.next();
        // Recovery copies must not become replacement targets on a later search.
        if (QFileInfo(path).fileName().contains(QStringLiteral(".8bitpad.bak-"))
            || QFileInfo(path).fileName().contains(QStringLiteral(".8bitpad.bak-"))) continue;
        ++result.scanned;
        QByteArray bytes;
        QString text;
        FileResult file;
        if (!read(path, options.maxFileBytes, bytes) || !decode(bytes, text, file.encoding)) { ++result.skipped; continue; }
        file.path = QFileInfo(path).absoluteFilePath();
        file.digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        auto matches = regex.globalMatch(text);
        qsizetype previousPosition = 0;
        qsizetype lineStart = 0;
        int line = 1;
        while (matches.hasNext()) {
            if (cancelled(cancel)) { result.cancelled = true; break; }
            if (result.matches >= options.maxMatches) { result.truncated = true; break; }
            const auto found = matches.next();
            Match match;
            match.start = found.capturedStart();
            match.length = found.capturedLength();
            for (qsizetype i = previousPosition; i < match.start; ++i) {
                if (text[i] == QLatin1Char('\n')) { ++line; lineStart = i + 1; }
                else if (text[i] == QLatin1Char('\r') && (i + 1 >= text.size() || text[i + 1] != QLatin1Char('\n'))) { ++line; lineStart = i + 1; }
            }
            previousPosition = match.start;
            match.line = line;
            match.column = int(match.start - lineStart + 1);
            match.preview = text.mid(lineStart, 240).section(QRegularExpression(QStringLiteral("[\\r\\n]")), 0, 0);
            match.captures = found.capturedTexts();
            for (const auto &capture : match.captures) captureCharacters += capture.size();
            if (captureCharacters > 4 * 1024 * 1024) { result.truncated = true; break; }
            file.matches.append(match);
            ++result.matches;
        }
        if (!file.matches.isEmpty()) result.files.append(file);
        if (result.cancelled || result.truncated) break;
    }
    return result;
}

ReplaceResult replace(const Result &preview, const QString &replacement,
                      const QStringList &openFiles, const Cancellation &cancel)
{
    ReplaceResult result;
    if (!preview.error.isEmpty() || preview.cancelled || preview.truncated) {
        result.errors.append(QStringLiteral("Run a complete, untruncated preview before replacing."));
        return result;
    }
    QSet<QString> blocked;
    for (const auto &path : openFiles) blocked.insert(normalizedPath(path));
    for (const auto &file : preview.files) {
        if (cancelled(cancel)) { result.cancelled = true; break; }
        if (blocked.contains(normalizedPath(file.path))) {
            ++result.skipped;
            result.errors.append(QStringLiteral("Open document skipped: %1").arg(file.path));
            continue;
        }
        QByteArray original;
        QString text;
        Encoding encoding;
        if (!read(file.path, preview.options.maxFileBytes, original) ||
            QCryptographicHash::hash(original, QCryptographicHash::Sha256) != file.digest ||
            !decode(original, text, encoding)) {
            ++result.skipped;
            result.errors.append(QStringLiteral("Changed or unreadable file skipped: %1").arg(file.path));
            continue;
        }
        QString updated;
        qsizetype previousEnd = 0;
        bool tooLarge = false;
        for (const auto &match : file.matches) {
            const QString inserted = replacementFor(replacement, match, preview.options.regex);
            if (updated.size() + match.start - previousEnd + inserted.size() > 64 * 1024 * 1024) {
                tooLarge = true;
                break;
            }
            updated += QStringView(text).sliced(previousEnd, match.start - previousEnd);
            updated += inserted;
            previousEnd = match.start + match.length;
        }
        if (tooLarge || updated.size() + text.size() - previousEnd > 64 * 1024 * 1024) {
            ++result.skipped;
            result.errors.append(QStringLiteral("Replacement output too large: %1").arg(file.path));
            continue;
        }
        updated += QStringView(text).sliced(previousEnd);
        const QByteArray output = encode(updated, encoding);
        if (output == original) continue;

        const QString backup = file.path + QStringLiteral(".8bitpad.bak-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QFile::copy(file.path, backup)) {
            ++result.skipped;
            result.errors.append(QStringLiteral("Could not create backup: %1").arg(file.path));
            continue;
        }
        result.backups.append(backup);
        QByteArray backupBytes;
        QByteArray latest;
        if (!read(backup, preview.options.maxFileBytes, backupBytes) ||
            QCryptographicHash::hash(backupBytes, QCryptographicHash::Sha256) != file.digest ||
            !read(file.path, preview.options.maxFileBytes, latest) ||
            QCryptographicHash::hash(latest, QCryptographicHash::Sha256) != file.digest) {
            ++result.skipped;
            result.errors.append(QStringLiteral("File changed while backing up: %1").arg(file.path));
            continue;
        }
        if (cancelled(cancel)) { result.cancelled = true; break; }
        QSaveFile destination(file.path);
        destination.setDirectWriteFallback(false);
        if (!destination.open(QIODevice::WriteOnly) || destination.write(output) != output.size() || !destination.commit()) {
            ++result.skipped;
            result.errors.append(QStringLiteral("Save failed; backup retained: %1").arg(file.path));
            continue;
        }
        ++result.files;
        result.matches += file.matches.size();
    }
    return result;
}
}
