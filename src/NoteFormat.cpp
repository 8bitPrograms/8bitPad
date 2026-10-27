// SPDX-License-Identifier: GPL-3.0-or-later
#include "NoteFormat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

QString expandNotePattern(QString pattern, const NoteContext &context)
{
    const QDateTime when = context.when.isValid() ? context.when : QDateTime::currentDateTime();
    const QString counter = QString::number(qMax(1, context.counter));
    const struct { const char *token; QString value; } tokens[] = {
        {"{datetime}", when.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"))},
        {"{date}", when.toString(QStringLiteral("yyyy-MM-dd"))},
        {"{time}", when.toString(QStringLiteral("HH-mm-ss"))},
        {"{year}", when.toString(QStringLiteral("yyyy"))},
        {"{month}", when.toString(QStringLiteral("MM"))},
        {"{day}", when.toString(QStringLiteral("dd"))},
        {"{hour}", when.toString(QStringLiteral("HH"))},
        {"{minute}", when.toString(QStringLiteral("mm"))},
        {"{second}", when.toString(QStringLiteral("ss"))},
        {"{counter}", counter},
        {"{n}", counter},
        {"{selection}", context.selection},
        {"{clipboard}", context.clipboard},
        {"{user}", context.user},
    };
    for (const auto &token : tokens)
        pattern.replace(QLatin1String(token.token), token.value, Qt::CaseInsensitive);
    pattern.replace(QStringLiteral("%Y"), when.toString(QStringLiteral("yyyy")));
    pattern.replace(QStringLiteral("%m"), when.toString(QStringLiteral("MM")));
    pattern.replace(QStringLiteral("%d"), when.toString(QStringLiteral("dd")));
    pattern.replace(QStringLiteral("%H"), when.toString(QStringLiteral("HH")));
    pattern.replace(QStringLiteral("%M"), when.toString(QStringLiteral("mm")));
    pattern.replace(QStringLiteral("%S"), when.toString(QStringLiteral("ss")));
    return pattern;
}

QString sanitizeNoteFileName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")), QStringLiteral(" "));
    name = name.simplified();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("note");
    if (name.size() > 120)
        name.truncate(120);
    return name.trimmed();
}

QString noteFileName(const QString &pattern, const QString &extension, const NoteContext &context)
{
    QString ext = extension.trimmed();
    while (ext.startsWith(QLatin1Char('.')))
        ext.remove(0, 1);
    ext.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")), QString());
    if (ext.isEmpty())
        ext = QStringLiteral("txt");
    return sanitizeNoteFileName(expandNotePattern(pattern, context)) + QLatin1Char('.') + ext.toLower();
}

QString createNoteFile(const QString &directory, const QString &pattern, const QString &extension,
                       const QString &templateText, const NoteContext &context, QString *error)
{
    QDir dir(directory);
    if (directory.trimmed().isEmpty() || !dir.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("The notes folder could not be created.");
        return {};
    }
    const QString fileName = noteFileName(pattern.trimmed().isEmpty() ? QStringLiteral("{date} {time}") : pattern, extension, context);
    QString path = dir.filePath(fileName);
    const QFileInfo info(path);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    int duplicate = 2;
    while (QFileInfo::exists(path) && duplicate < 1000) {
        path = dir.filePath(base + QLatin1Char(' ') + QString::number(duplicate) + QLatin1Char('.') + suffix);
        ++duplicate;
    }
    QString body = expandNotePattern(templateText, context);
    if (body.isEmpty())
        body = context.selection;
    if (!body.isEmpty() && !body.endsWith(QLatin1Char('\n')))
        body.append(QLatin1Char('\n'));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    file.write(body.toUtf8());
    return QFileInfo(path).absoluteFilePath();
}
