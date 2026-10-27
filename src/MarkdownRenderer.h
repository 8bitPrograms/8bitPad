// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#pragma once

#include <QFont>
#include <QList>
#include <QPalette>
#include <QTextDocument>

struct MarkdownHeading
{
    QString title;
    QString anchor;
    int level = 1;
    int sourceLine = 0;
    int documentPosition = 0;
};

struct MarkdownSyntaxColors
{
    QColor keyword, string, number, comment, type;
    bool operator==(const MarkdownSyntaxColors &) const = default;
};

struct MarkdownRenderOptions
{
    QFont textFont;
    QFont codeFont;
    QPalette palette;
    MarkdownSyntaxColors syntaxColors;
    int contentWidth = 800;
};

// No network resources, HTML execution, or files outside the source directory.
// Construct a new document for each render, so image caches cannot cross sources.
class MarkdownDocument final : public QTextDocument
{
public:
    explicit MarkdownDocument(const QString &sourcePath, QObject *parent = nullptr);
    QString sourcePath() const { return mSourcePath; }
    QString resourceRoot() const { return mResourceRoot; }

    // Canonical, existing local files only; rejects parent traversal and symlinks
    // escaping root, remote URLs, UNC paths, and non-local file authorities.
    static QString localFileWithinRoot(const QUrl &url, const QString &root);

protected:
    QVariant loadResource(int type, const QUrl &name) override;

private:
    QString mSourcePath;
    QString mResourceRoot;
};

class MarkdownRenderer
{
public:
    static QList<MarkdownHeading> render(MarkdownDocument *document,
                                        const QString &markdown,
                                        const MarkdownRenderOptions &options);
    static bool isMarkdownPath(const QString &path);
};
