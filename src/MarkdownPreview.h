// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#pragma once

#include "MarkdownRenderer.h"
#include <QDockWidget>
#include <QTimer>

class QComboBox;
class QFontComboBox;
class QLabel;
class QSpinBox;
class QTextBrowser;
class QTreeWidget;

// Feed the active editor's unsaved buffer through setMarkdown(). Rendering is
// debounced; this widget never writes a source file or edits the editor buffer.
class MarkdownPreview final : public QDockWidget
{
    Q_OBJECT
public:
    explicit MarkdownPreview(QWidget *parent = nullptr);
    QString sourcePath() const { return mSourcePath; }
    QString markdown() const { return mMarkdown; }
    QTextDocument *renderedDocument() const;
    const QList<MarkdownHeading> &headings() const { return mHeadings; }
    QFont textFont() const { return mTextFont; }
    QFont codeFont() const { return mCodeFont; }
    int zoomPercent() const { return mZoomPercent; }

public slots:
    void setMarkdown(const QString &text, const QString &sourcePath = QString());
    void renderNow();
    void refreshTheme();
    void setDocumentFont(const QFont &font);
    void setCodeFont(const QFont &font);
    void setZoomPercent(int percent);
    void setSyntaxColors(const MarkdownSyntaxColors &colors);

signals:
    void openSourceRequested(const QString &path, int zeroBasedLine);
    void documentRendered();
    void presentationChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void followLink(const QUrl &url);
    void scrollToHeading(int index);
    void updateSourceLabel();

    QTextBrowser *mBrowser = nullptr;
    QTreeWidget *mContents = nullptr;
    QFontComboBox *mFontFamily = nullptr;
    QSpinBox *mFontSize = nullptr;
    QComboBox *mZoom = nullptr;
    QLabel *mSourceLabel = nullptr;
    QLabel *mMessage = nullptr;
    QTimer mRenderTimer;
    QString mMarkdown;
    QString mSourcePath;
    QFont mTextFont;
    QFont mCodeFont;
    MarkdownSyntaxColors mSyntaxColors;
    QList<MarkdownHeading> mHeadings;
    int mZoomPercent = 100;
    int mLastWidth = 0;
    bool mDirty = true;
    bool mRendering = false;
};
