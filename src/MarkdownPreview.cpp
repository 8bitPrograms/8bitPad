// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#include "MarkdownPreview.h"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QEvent>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>
#include <algorithm>

MarkdownPreview::MarkdownPreview(QWidget *parent)
    : QDockWidget(tr("Markdown preview"), parent)
{
    setObjectName(QStringLiteral("MarkdownPreview"));
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setMinimumWidth(300);
    resize(600, 650);
    mTextFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    mTextFont.setPointSize(11);
    mCodeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    // Some headless Qt plugins report the generic "monospace" family even
    // when platform fonts are available. Choose a concrete installed family.
    const QStringList fontFamilies = QFontDatabase::families();
    for (const QString &family : {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                                 QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Liberation Mono")}) {
        if (fontFamilies.contains(family)) { mCodeFont.setFamily(family); break; }
    }
    mCodeFont.setPointSize(10);

    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *toolbar = new QToolBar(body);
    toolbar->setObjectName(QStringLiteral("MarkdownPreviewToolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));
    QAction *source = toolbar->addAction(tr("Source"));
    source->setToolTip(tr("Open the Markdown source"));
    connect(source, &QAction::triggered, this, [this] { emit openSourceRequested(mSourcePath, 0); });
    QAction *contents = toolbar->addAction(tr("Contents"));
    contents->setCheckable(true);
    contents->setToolTip(tr("Show headings. Double-click a heading to jump to its source line."));
    toolbar->addSeparator();

    auto *fontButton = new QToolButton(toolbar);
    fontButton->setText(tr("Aa"));
    fontButton->setAccessibleName(tr("Preview fonts"));
    fontButton->setToolTip(tr("Preview text and code fonts"));
    fontButton->setPopupMode(QToolButton::InstantPopup);
    auto *fontMenu = new QMenu(fontButton);
    auto *fontPanel = new QWidget(fontMenu);
    auto *fontLayout = new QFormLayout(fontPanel);
    fontLayout->setContentsMargins(12, 12, 12, 12);
    mFontFamily = new QFontComboBox(fontPanel);
    mFontFamily->setCurrentFont(mTextFont);
    mFontFamily->setAccessibleName(tr("Text font"));
    mFontSize = new QSpinBox(fontPanel);
    mFontSize->setRange(8, 32);
    mFontSize->setSuffix(tr(" pt"));
    mFontSize->setValue(mTextFont.pointSize());
    mFontSize->setAccessibleName(tr("Text size"));
    auto *codeFamily = new QFontComboBox(fontPanel);
    codeFamily->setFontFilters(QFontComboBox::MonospacedFonts);
    codeFamily->setCurrentFont(mCodeFont);
    codeFamily->setAccessibleName(tr("Code font"));
    auto *codeSize = new QSpinBox(fontPanel);
    codeSize->setRange(7, 32);
    codeSize->setSuffix(tr(" pt"));
    codeSize->setValue(mCodeFont.pointSize());
    codeSize->setAccessibleName(tr("Code size"));
    fontLayout->addRow(tr("Text font"), mFontFamily);
    fontLayout->addRow(tr("Text size"), mFontSize);
    fontLayout->addRow(tr("Code font"), codeFamily);
    fontLayout->addRow(tr("Code size"), codeSize);
    auto *fontAction = new QWidgetAction(fontMenu);
    fontAction->setDefaultWidget(fontPanel);
    fontMenu->addAction(fontAction);
    fontButton->setMenu(fontMenu);
    toolbar->addWidget(fontButton);
    connect(mFontFamily, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        QFont selected = mTextFont;
        selected.setFamily(font.family());
        setDocumentFont(selected);
    });
    connect(mFontSize, &QSpinBox::valueChanged, this, [this](int size) {
        QFont selected = mTextFont;
        selected.setPointSize(size);
        setDocumentFont(selected);
    });
    connect(codeFamily, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        QFont selected = mCodeFont;
        selected.setFamily(font.family());
        setCodeFont(selected);
    });
    connect(codeSize, &QSpinBox::valueChanged, this, [this](int size) {
        QFont selected = mCodeFont;
        selected.setPointSize(size);
        setCodeFont(selected);
    });
    connect(this, &MarkdownPreview::presentationChanged, codeFamily, [this, codeFamily, codeSize] {
        const QSignalBlocker familySignals(codeFamily), sizeSignals(codeSize);
        codeFamily->setCurrentFont(mCodeFont);
        codeSize->setValue(qRound(mCodeFont.pointSizeF()));
    });

    mZoom = new QComboBox(toolbar);
    mZoom->setAccessibleName(tr("Preview zoom"));
    mZoom->setToolTip(tr("Zoom preview (Ctrl+mouse wheel)"));
    for (int percent : {75, 90, 100, 110, 125, 150, 175, 200})
        mZoom->addItem(QString::number(percent) + '%', percent);
    mZoom->setCurrentIndex(mZoom->findData(100));
    toolbar->addWidget(mZoom);
    connect(mZoom, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) setZoomPercent(mZoom->itemData(index).toInt());
    });
    layout->addWidget(toolbar);
    mSourceLabel = new QLabel(body);
    mSourceLabel->setContentsMargins(12, 5, 12, 5);
    mSourceLabel->setTextFormat(Qt::PlainText);
    mSourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mSourceLabel);

    auto *splitter = new QSplitter(Qt::Horizontal, body);
    mContents = new QTreeWidget(splitter);
    mContents->setObjectName(QStringLiteral("MarkdownContents"));
    mContents->setHeaderHidden(true);
    mContents->setRootIsDecorated(true);
    mContents->setIndentation(12);
    mContents->setMinimumWidth(130);
    mContents->setMaximumWidth(280);
    mContents->setAccessibleName(tr("Markdown table of contents"));
    mContents->hide();
    mBrowser = new QTextBrowser(splitter);
    mBrowser->setObjectName(QStringLiteral("MarkdownPreviewText"));
    mBrowser->setAccessibleName(tr("Rendered Markdown"));
    mBrowser->setFrameShape(QFrame::NoFrame);
    mBrowser->setOpenLinks(false);
    mBrowser->setOpenExternalLinks(false);
    mBrowser->setReadOnly(true);
    mBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction);
    mBrowser->setAcceptRichText(false);
    mBrowser->viewport()->installEventFilter(this);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    connect(contents, &QAction::toggled, mContents, &QWidget::setVisible);
    connect(mContents, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        scrollToHeading(item->data(0, Qt::UserRole).toInt());
    });
    connect(mContents, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < mHeadings.size()) emit openSourceRequested(mSourcePath, mHeadings.at(index).sourceLine);
    });
    connect(mBrowser, &QTextBrowser::anchorClicked, this, &MarkdownPreview::followLink);

    mMessage = new QLabel(body);
    mMessage->setObjectName(QStringLiteral("MarkdownPreviewMessage"));
    mMessage->setWordWrap(true);
    mMessage->setTextFormat(Qt::PlainText);
    mMessage->setContentsMargins(12, 5, 12, 5);
    mMessage->hide();
    layout->addWidget(mMessage);
    setWidget(body);
    mRenderTimer.setSingleShot(true);
    mRenderTimer.setInterval(180);
    connect(&mRenderTimer, &QTimer::timeout, this, &MarkdownPreview::renderNow);

    const auto addZoomShortcut = [this](const QKeySequence &key, int delta) {
        auto *shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, delta] { setZoomPercent(delta ? mZoomPercent + delta : 100); });
    };
    addZoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), 10);
    addZoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), 10);
    addZoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), -10);
    addZoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), 0);
    updateSourceLabel();
}

QTextDocument *MarkdownPreview::renderedDocument() const
{
    return mBrowser->document();
}

void MarkdownPreview::setMarkdown(const QString &text, const QString &sourcePath)
{
    if (mMarkdown == text && mSourcePath == sourcePath) return;
    const bool changedSource = mSourcePath != sourcePath;
    mMarkdown = text;
    mSourcePath = sourcePath;
    mDirty = true;
    if (changedSource) mBrowser->verticalScrollBar()->setValue(0);
    updateSourceLabel();
    if (isVisible()) mRenderTimer.start();
}

void MarkdownPreview::renderNow()
{
    if (mRendering || !mDirty) return;
    mRenderTimer.stop();
    mRendering = true;
    const int oldMaximum = mBrowser->verticalScrollBar()->maximum();
    const qreal scroll = oldMaximum > 0 ? qreal(mBrowser->verticalScrollBar()->value()) / oldMaximum : 0;
    const QTextCursor oldCursor = mBrowser->textCursor();
    const int selectionStart = oldCursor.anchor(), selectionEnd = oldCursor.position();
    auto *document = new MarkdownDocument(mSourcePath, mBrowser);
    MarkdownRenderOptions options;
    options.palette = palette();
    options.syntaxColors = mSyntaxColors;
    options.textFont = mTextFont;
    options.textFont.setPointSizeF(mTextFont.pointSizeF() * mZoomPercent / 100.);
    options.codeFont = mCodeFont;
    options.codeFont.setPointSizeF(mCodeFont.pointSizeF() * mZoomPercent / 100.);
    options.contentWidth = std::max(80, mBrowser->viewport()->width() - 48);
    mLastWidth = mBrowser->viewport()->width();
    const bool tooLarge = mMarkdown.size() > 4 * 1024 * 1024 || mMarkdown.toUtf8().size() > 4 * 1024 * 1024;
    mHeadings = MarkdownRenderer::render(document, tooLarge ? QString() : mMarkdown, options);
    const QPointer<QTextDocument> previous = mBrowser->document();
    mBrowser->setDocument(document);
    if (previous && previous->parent() == mBrowser) previous->deleteLater();
    mContents->clear();
    QList<QTreeWidgetItem *> parents;
    QList<int> levels;
    for (int index = 0; index < mHeadings.size(); ++index) {
        const auto &heading = mHeadings.at(index);
        while (!levels.isEmpty() && levels.last() >= heading.level) { levels.removeLast(); parents.removeLast(); }
        auto *item = parents.isEmpty() ? new QTreeWidgetItem(mContents) : new QTreeWidgetItem(parents.last());
        item->setText(0, heading.title);
        item->setData(0, Qt::UserRole, index);
        item->setToolTip(0, tr("Source line %1").arg(heading.sourceLine + 1));
        item->setExpanded(true);
        parents.append(item);
        levels.append(heading.level);
    }
    QTextCursor restored(document);
    const int lastPosition = std::max(0, document->characterCount() - 1);
    restored.setPosition(std::clamp(selectionStart, 0, lastPosition));
    restored.setPosition(std::clamp(selectionEnd, 0, lastPosition), QTextCursor::KeepAnchor);
    mBrowser->setTextCursor(restored);
    mBrowser->verticalScrollBar()->setValue(qRound(scroll * mBrowser->verticalScrollBar()->maximum()));
    mMessage->setText(tooLarge ? tr("Preview is limited to 4 MiB. The source document is unchanged.") : QString());
    mMessage->setVisible(tooLarge);
    mDirty = false;
    mRendering = false;
    emit documentRendered();
}

void MarkdownPreview::refreshTheme()
{
    mDirty = true;
    if (isVisible()) mRenderTimer.start();
}

void MarkdownPreview::setDocumentFont(const QFont &font)
{
    QFont selected = font;
    selected.setPointSizeF(std::clamp(font.pointSizeF() > 0 ? font.pointSizeF() : 11., 8., 32.));
    if (selected == mTextFont) return;
    mTextFont = selected;
    const QSignalBlocker familySignals(mFontFamily), sizeSignals(mFontSize);
    mFontFamily->setCurrentFont(selected);
    mFontSize->setValue(qRound(selected.pointSizeF()));
    mDirty = true;
    if (isVisible()) mRenderTimer.start();
    emit presentationChanged();
}

void MarkdownPreview::setCodeFont(const QFont &font)
{
    QFont selected = font;
    selected.setPointSizeF(std::clamp(font.pointSizeF() > 0 ? font.pointSizeF() : 10., 7., 32.));
    if (selected == mCodeFont) return;
    mCodeFont = selected;
    mDirty = true;
    if (isVisible()) mRenderTimer.start();
    emit presentationChanged();
}

void MarkdownPreview::setZoomPercent(int percent)
{
    percent = std::clamp(percent, 50, 250);
    if (percent == mZoomPercent) return;
    mZoomPercent = percent;
    const QSignalBlocker block(mZoom);
    int index = mZoom->findData(percent);
    if (index < 0) { mZoom->addItem(QString::number(percent) + '%', percent); index = mZoom->count() - 1; }
    mZoom->setCurrentIndex(index);
    mDirty = true;
    if (isVisible()) mRenderTimer.start();
    emit presentationChanged();
}

void MarkdownPreview::setSyntaxColors(const MarkdownSyntaxColors &colors)
{
    if (colors == mSyntaxColors) return;
    mSyntaxColors = colors;
    refreshTheme();
}

void MarkdownPreview::scrollToHeading(int index)
{
    if (index < 0 || index >= mHeadings.size()) return;
    mBrowser->scrollToAnchor(mHeadings.at(index).anchor);
}

void MarkdownPreview::followLink(const QUrl &url)
{
    if (url.scheme().isEmpty() && url.path().isEmpty() && url.hasFragment()) {
        mBrowser->scrollToAnchor(url.fragment(QUrl::FullyDecoded));
        return;
    }
    if (url.scheme() == "https" || url.scheme() == "http" || url.scheme() == "mailto") {
        // Only called by QTextBrowser::anchorClicked after an explicit action.
        QDesktopServices::openUrl(url);
        return;
    }
    auto *document = static_cast<MarkdownDocument *>(mBrowser->document());
    const QString local = MarkdownDocument::localFileWithinRoot(url, document->resourceRoot());
    if (!local.isEmpty()) {
        if (QFileInfo(local).canonicalFilePath() == QFileInfo(mSourcePath).canonicalFilePath() && url.hasFragment())
            mBrowser->scrollToAnchor(url.fragment(QUrl::FullyDecoded));
        else emit openSourceRequested(local, 0);
        return;
    }
    mMessage->setText(tr("This link is unavailable. Local links must stay inside the Markdown file's folder."));
    mMessage->show();
}

void MarkdownPreview::updateSourceLabel()
{
    mSourceLabel->setText(mSourcePath.isEmpty() ? tr("Unsaved Markdown · live preview")
                                             : QFileInfo(mSourcePath).fileName() + tr(" · live preview"));
    mSourceLabel->setToolTip(mSourcePath + tr("\nLocal raster images are supported. Remote images, raw HTML, Mermaid and math rendering are not enabled."));
}

bool MarkdownPreview::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == mBrowser->viewport()) {
        if (event->type() == QEvent::Resize && !mRendering && std::abs(mLastWidth - mBrowser->viewport()->width()) > 24) {
            mDirty = true;
            if (isVisible()) mRenderTimer.start();
        } else if (event->type() == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            if (wheel->modifiers().testFlag(Qt::ControlModifier) && wheel->angleDelta().y()) {
                setZoomPercent(mZoomPercent + (wheel->angleDelta().y() > 0 ? 10 : -10));
                wheel->accept();
                return true;
            }
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void MarkdownPreview::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if ((event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
            && mBrowser && !mRendering) refreshTheme();
}

void MarkdownPreview::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    if (mDirty) renderNow();
}
