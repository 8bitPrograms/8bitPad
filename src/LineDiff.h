// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct DiffRow
{
    enum Kind { Equal, Delete, Insert, Change } kind = Equal;
    QString left;
    QString right;
};

struct DiffResult
{
    QVector<DiffRow> rows;
    bool truncated = false;
};

// Line diff capped for interactive use. Rows reconstruct both inputs,
// aside from a truncated tail that is reported and omitted.
DiffResult diffLines(const QStringList &left, const QStringList &right);
QStringList splitLines(const QString &text);
