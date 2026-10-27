/* Copyright 2026 8bitPad contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FUNCTIONEXTRACTOR_H
#define FUNCTIONEXTRACTOR_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>

struct NavigationSymbol {
    QString name;
    QString kind;
    int line = 0; // zero-based document line, never a wrapped display line
};

struct OverviewSpan {
    int indentation = 0;
    int width = 0;
};

struct DocumentOverview {
    QVector<OverviewSpan> rows;
    int lineCount = 1;
};

namespace FunctionExtractor {
using CancelCheck = std::function<bool()>;
constexpr qsizetype MaximumDocumentBytes = 20 * 1024 * 1024;
constexpr int MaximumSymbols = 5000;
constexpr int MaximumOverviewRows = 4096;
// Pure, bounded helpers. They parse text only; no widget or editor access.
// A heuristic outline is intentionally not a compiler/language-server index.
QVector<NavigationSymbol> extract(const QByteArray &utf8, const QString &language, const QString &fileName = {}, const CancelCheck &cancelled = {});
DocumentOverview overview(const QByteArray &utf8, const CancelCheck &cancelled = {});
QString languageFor(const QString &language, const QString &fileName);
}

#endif
