// SPDX-License-Identifier: GPL-3.0-or-later
#include "LineDiff.h"

namespace {
enum class Op { Equal, Delete, Insert };

struct Edit
{
    Op op;
    int left = -1;
    int right = -1;
};

constexpr int kMaxLines = 5000;
constexpr qint64 kMaxCells = 350000;

void appendFallback(const QStringList &left, const QStringList &right, int a0, int a1, int b0, int b1, QVector<Edit> &out)
{
    for (int i = a0; i < a1; ++i)
        out.append({Op::Delete, i, -1});
    for (int j = b0; j < b1; ++j)
        out.append({Op::Insert, -1, j});
}

void appendDp(const QStringList &left, const QStringList &right, int a0, int a1, int b0, int b1, QVector<Edit> &out)
{
    const int n = a1 - a0;
    const int m = b1 - b0;
    if (n == 0 && m == 0)
        return;
    if (n == 0) {
        for (int j = b0; j < b1; ++j)
            out.append({Op::Insert, -1, j});
        return;
    }
    if (m == 0) {
        for (int i = a0; i < a1; ++i)
            out.append({Op::Delete, i, -1});
        return;
    }
    if (qint64(n) * qint64(m) > kMaxCells) {
        appendFallback(left, right, a0, a1, b0, b1, out);
        return;
    }

    const int cols = m + 1;
    QVector<quint8> from((n + 1) * cols, 0);
    QVector<int> prev(cols, 0), cur(cols, 0);
    for (int j = 1; j <= m; ++j)
        from[j] = 2;
    for (int i = 1; i <= n; ++i) {
        from[i * cols] = 1;
        for (int j = 1; j <= m; ++j) {
            if (left.at(a0 + i - 1) == right.at(b0 + j - 1)) {
                cur[j] = prev[j - 1] + 1;
                from[i * cols + j] = 0;
            } else if (prev[j] >= cur[j - 1]) {
                cur[j] = prev[j];
                from[i * cols + j] = 1;
            } else {
                cur[j] = cur[j - 1];
                from[i * cols + j] = 2;
            }
        }
        prev.swap(cur);
        cur.fill(0);
    }

    QVector<Edit> reversed;
    reversed.reserve(n + m);
    int i = n;
    int j = m;
    while (i > 0 || j > 0) {
        const quint8 way = i == 0 ? 2 : j == 0 ? 1 : from[i * cols + j];
        if (way == 0) {
            reversed.append({Op::Equal, a0 + i - 1, b0 + j - 1});
            --i;
            --j;
        } else if (way == 1) {
            reversed.append({Op::Delete, a0 + i - 1, -1});
            --i;
        } else {
            reversed.append({Op::Insert, -1, b0 + j - 1});
            --j;
        }
    }
    for (int index = reversed.size() - 1; index >= 0; --index)
        out.append(reversed.at(index));
}

QVector<DiffRow> pairEdits(const QVector<Edit> &edits, const QStringList &left, const QStringList &right)
{
    QVector<DiffRow> rows;
    for (int i = 0; i < edits.size();) {
        if (edits.at(i).op == Op::Equal) {
            rows.append({DiffRow::Equal, left.at(edits.at(i).left), right.at(edits.at(i).right)});
            ++i;
            continue;
        }
        // LCS backtracking may emit insertions before deletions. Pair the
        // entire changed span so substitutions align on one displayed row.
        QVector<int> deleted;
        QVector<int> inserted;
        while (i < edits.size() && edits.at(i).op != Op::Equal) {
            const Edit &edit = edits.at(i++);
            if (edit.op == Op::Delete)
                deleted.append(edit.left);
            else
                inserted.append(edit.right);
        }
        const int paired = qMin(deleted.size(), inserted.size());
        for (int n = 0; n < paired; ++n)
            rows.append({DiffRow::Change, left.at(deleted.at(n)), right.at(inserted.at(n))});
        for (int n = paired; n < deleted.size(); ++n)
            rows.append({DiffRow::Delete, left.at(deleted.at(n)), QString()});
        for (int n = paired; n < inserted.size(); ++n)
            rows.append({DiffRow::Insert, QString(), right.at(inserted.at(n))});
    }
    return rows;
}
}

QStringList splitLines(const QString &text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (normalized.endsWith(QLatin1Char('\n')))
        normalized.chop(1);
    if (normalized.isEmpty())
        return {};
    return normalized.split(QLatin1Char('\n'));
}

DiffResult diffLines(const QStringList &left, const QStringList &right)
{
    DiffResult result;
    const int leftCount = qMin(left.size(), kMaxLines);
    const int rightCount = qMin(right.size(), kMaxLines);
    result.truncated = left.size() > kMaxLines || right.size() > kMaxLines;
    const QStringList a = result.truncated ? left.mid(0, leftCount) : left;
    const QStringList b = result.truncated ? right.mid(0, rightCount) : right;

    int prefix = 0;
    const int limit = qMin(a.size(), b.size());
    while (prefix < limit && a.at(prefix) == b.at(prefix))
        ++prefix;
    int suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
           a.at(a.size() - 1 - suffix) == b.at(b.size() - 1 - suffix))
        ++suffix;

    QVector<Edit> edits;
    edits.reserve(a.size() + b.size());
    for (int i = 0; i < prefix; ++i)
        edits.append({Op::Equal, i, i});
    appendDp(a, b, prefix, a.size() - suffix, prefix, b.size() - suffix, edits);
    for (int i = 0; i < suffix; ++i) {
        const int leftIndex = a.size() - suffix + i;
        const int rightIndex = b.size() - suffix + i;
        edits.append({Op::Equal, leftIndex, rightIndex});
    }
    result.rows = pairEdits(edits, a, b);
    return result;
}
