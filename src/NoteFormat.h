// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>

struct NoteContext
{
    QDateTime when;
    int counter = 1;
    QString selection;
    QString clipboard;
    QString user;
};

// Tokens: {date} {time} {datetime} {year} {month} {day} {hour} {minute}
// {second} {counter} {n} {selection} {clipboard} {user}
// plus %Y %m %d %H %M %S.
QString expandNotePattern(QString pattern, const NoteContext &context);
QString sanitizeNoteFileName(QString name);
QString noteFileName(const QString &pattern, const QString &extension, const NoteContext &context);

// Writes a UTF-8 note and returns its absolute path. Empty means error is set.
QString createNoteFile(const QString &directory, const QString &pattern, const QString &extension,
                       const QString &templateText, const NoteContext &context, QString *error);
