// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#include "MarkdownPreview.h"
#include "MarkdownRenderer.h"

#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>
#include <QtTest>
#include <cmath>

namespace {
MarkdownRenderOptions options(bool dark = false)
{
    MarkdownRenderOptions value;
    value.textFont = QFont(QStringLiteral("Segoe UI"), 12);
    value.codeFont = QFont(QStringLiteral("Consolas"), 11);
    value.palette.setColor(QPalette::Base, dark ? QColor("#0b0e14") : QColor("#ffffff"));
    value.palette.setColor(QPalette::Text, dark ? QColor("#c7c7c7") : QColor("#24292f"));
    return value;
}

QTextCharFormat formatAt(QTextDocument &document, const QString &needle, int offset = 0)
{
    const int position = document.toPlainText().indexOf(needle) + offset;
    QTextCursor cursor(&document);
    cursor.setPosition(position);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    return cursor.charFormat();
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), qint64(contents.size()));
}
}

class MarkdownPreviewTest : public QObject
{
    Q_OBJECT
private slots:
    void githubMarkdownRetainsStructure()
    {
        const QString source = QStringLiteral(
            "# Project **guide**\n\n"
            "A [reference](https://example.com) and `inline code` with ~~old text~~.\n\n"
            "- [x] Completed item\n- [ ] Pending item\n\n"
            "| Name | Value |\n| --- | ---: |\n| alpha | 12 |\n\n"
            "> A useful quotation\n\n"
            "## Details\n\nSecond paragraph.\n\n"
            "## Details\n\n"
            "```python\n# Not a heading\ndef greet(name):\n    return \"hello\" + str(42)\n```\n");
        MarkdownDocument document({});
        const auto headings = MarkdownRenderer::render(&document, source, options());
        QCOMPARE(headings.size(), 3);
        QCOMPARE(headings.at(0).title, QString("Project guide"));
        QCOMPARE(headings.at(0).anchor, QString("project-guide"));
        QCOMPARE(headings.at(1).anchor, QString("details"));
        QCOMPARE(headings.at(2).anchor, QString("details-1"));
        QCOMPARE(headings.at(0).sourceLine, 0);
        QVERIFY(document.toPlainText().contains("Completed item"));
        QVERIFY(formatAt(document, "old text").fontStrikeOut());
        QVERIFY(formatAt(document, "inline code").fontFixedPitch());
        QCOMPARE(formatAt(document, "reference").anchorHref(), QString("https://example.com"));
        QVERIFY(formatAt(document, "Project").fontPointSize() > 12);
        bool checked = false, unchecked = false, quote = false;
        for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
            checked |= block.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked;
            unchecked |= block.blockFormat().marker() == QTextBlockFormat::MarkerType::Unchecked;
            quote |= block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0;
            if (block.text() == "Pending item") {
                QCOMPARE(block.blockFormat().marker(), QTextBlockFormat::MarkerType::Unchecked);
                QCOMPARE(block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel), 0);
                QCOMPARE(block.blockFormat().background().style(), Qt::NoBrush);
            }
            if (block.blockFormat().headingLevel() > 0)
                QCOMPARE(block.blockFormat().background().style(), Qt::NoBrush);
        }
        QVERIFY(checked);
        QVERIFY(unchecked);
        QVERIFY(quote);
        QTextTable *table = nullptr;
        for (auto *frame : document.rootFrame()->childFrames()) if (auto *candidate = qobject_cast<QTextTable *>(frame)) table = candidate;
        QVERIFY(table);
        QCOMPARE(table->rows(), 2);
        QCOMPARE(table->columns(), 2);
        QVERIFY(table->format().cellPadding() >= 8);
        QVERIFY(table->cellAt(0, 0).format().background().color().isValid());
    }

    void fencedCodeIsStyledWithoutChangingText()
    {
        const QString markdown = QStringLiteral(
            "```python\ndef greet(name):\n    # return is a comment\n    return \"hello\" + str(42)\n```\n\n"
            "```json\n{\"enabled\": true, \"count\": 12}\n```\n\n"
            "```yaml\ntrigger: main\nsteps:\n  - script: echo hello\n```\n");
        MarkdownDocument document({});
        MarkdownRenderer::render(&document, markdown, options(true));
        const QString plain = document.toPlainText();
        QVERIFY(plain.contains("def greet(name):\n    # return is a comment\n    return \"hello\" + str(42)"));
        const QColor normal = options(true).palette.color(QPalette::Text);
        const auto keyword = formatAt(document, "def greet");
        QVERIFY(keyword.fontFixedPitch());
        QVERIFY(keyword.foreground().color() != normal);
        QVERIFY(formatAt(document, "hello").foreground().color() != keyword.foreground().color());
        QVERIFY(formatAt(document, "42").foreground().color() != normal);
        QVERIFY(formatAt(document, "# return").foreground().color() != keyword.foreground().color());
        QVERIFY(formatAt(document, "enabled").foreground().color() != normal);
        QVERIFY(formatAt(document, "trigger").foreground().color() != normal);
        bool background = false;
        for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
            if (block.text().startsWith("def greet")) background = block.blockFormat().background().style() != Qt::NoBrush;
        QVERIFY(background);
    }

    void localImagesCannotEscapeSourceFolder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkdir("docs"));
        QVERIFY(QDir(directory.path()).mkdir("docs/assets"));
        const QString root = directory.path() + "/docs";
        QImage image(32, 20, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(root + "/assets/sample image.png"));
        QVERIFY(image.save(directory.path() + "/outside.png"));
        writeFile(root + "/readme.md", "# Images");
        MarkdownDocument document(root + "/readme.md");
        auto load = [&](const QString &url) { return qvariant_cast<QImage>(document.resource(QTextDocument::ImageResource, QUrl(url))); };
        QCOMPARE(load("assets/sample%20image.png").size(), QSize(32, 20));
        QCOMPARE(load("../outside.png").size(), QSize(1, 1));
        QCOMPARE(load("%2e%2e/outside.png").size(), QSize(1, 1));
        QCOMPARE(load(QUrl::fromLocalFile(directory.path() + "/outside.png").toString()).size(), QSize(1, 1));
        QCOMPARE(load("https://example.com/tracker.png").size(), QSize(1, 1));
        QCOMPARE(load("file://server/share/tracker.png").size(), QSize(1, 1));
        QCOMPARE(load("data:image/png;base64,AAAA").size(), QSize(1, 1));
        QVERIFY(MarkdownDocument::localFileWithinRoot(QUrl("assets/sample%20image.png"), root).endsWith("sample image.png"));
        QVERIFY(MarkdownDocument::localFileWithinRoot(QUrl("../outside.png"), root).isEmpty());
        QVERIFY(MarkdownDocument::localFileWithinRoot(QUrl("javascript:alert(1)"), root).isEmpty());
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(directory.path() + "/outside.png", root + "/escape.png"));
        QCOMPARE(load("escape.png").size(), QSize(1, 1));
#endif
    }

    void htmlIsInertAndThemePreservesContent()
    {
        const QString markdown = QStringLiteral("# Heading\n\nPlain **bold** and `inline`.\n\n"
            "<img src=\"file:///private/image.png\">\n\n<script>alert('test')</script>\n\n"
            "```cpp\nconst int answer = 42;\n```\n");
        MarkdownDocument light({}), dark({});
        MarkdownRenderer::render(&light, markdown, options(false));
        auto darkOptions = options(true);
        darkOptions.textFont.setPointSize(16);
        darkOptions.codeFont.setPointSize(14);
        MarkdownRenderer::render(&dark, markdown, darkOptions);
        QCOMPARE(light.toPlainText(), dark.toPlainText());
        QCOMPARE(light.rootFrame()->frameFormat().background().color(), QColor("#ffffff"));
        QCOMPARE(dark.rootFrame()->frameFormat().background().color(), QColor("#0b0e14"));
        QVERIFY(formatAt(light, "Plain").foreground().color() != formatAt(dark, "Plain").foreground().color());
        QVERIFY(formatAt(dark, "Heading").fontPointSize() > formatAt(light, "Heading").fontPointSize());
        for (QTextBlock block = dark.begin(); block.isValid(); block = block.next())
            for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment)
                QVERIFY(!fragment.fragment().charFormat().isImageFormat());
        QCOMPARE(formatAt(dark, "answer").fontPointSize(), 14.);
    }

    void themeTokensAreUsedAndLowContrastIsCorrected()
    {
        auto renderOptions = options(true);
        renderOptions.syntaxColors = {QColor("#ffc875"), QColor("#80ebbb"), QColor("#98cfff"),
                                     QColor("#15171a"), QColor("#e1a8ff")};
        renderOptions.palette.setColor(QPalette::Link, QColor("#1b1b20"));
        MarkdownDocument document({});
        MarkdownRenderer::render(&document, "```python\ndef test():\n    # quiet comment\n    return \"value\" + str(42)\n```\n\n[Link](https://example.com)", renderOptions);
        QCOMPARE(formatAt(document, "def").foreground().color(), renderOptions.syntaxColors.keyword);
        QCOMPARE(formatAt(document, "value").foreground().color(), renderOptions.syntaxColors.string);
        QCOMPARE(formatAt(document, "42").foreground().color(), renderOptions.syntaxColors.number);
        const auto luminance = [](const QColor &color) {
            const auto channel = [](double value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
            return .2126 * channel(color.redF()) + .7152 * channel(color.greenF()) + .0722 * channel(color.blueF());
        };
        const auto contrast = [&luminance](const QColor &a, const QColor &b) {
            const double first = luminance(a), second = luminance(b);
            return (std::max(first, second) + .05) / (std::min(first, second) + .05);
        };
        const auto commentBlock = document.findBlock(document.toPlainText().indexOf("# quiet"));
        QVERIFY(contrast(formatAt(document, "# quiet").foreground().color(), commentBlock.blockFormat().background().color()) >= 4.5);
        QVERIFY(contrast(formatAt(document, "Link").foreground().color(), renderOptions.palette.color(QPalette::Base)) >= 4.5);
    }

    void livePreviewUsesDirtyBufferAndHonorsDebounce()
    {
        QTemporaryDir directory;
        const QString source = directory.path() + "/draft.md";
        writeFile(source, "# Saved copy\n");
        MarkdownPreview preview;
        preview.show();
        QSignalSpy rendered(&preview, &MarkdownPreview::documentRendered);
        preview.setMarkdown("# Unsaved first\n", source);
        preview.setMarkdown("# Unsaved final\n", source);
        QCOMPARE(rendered.size(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(rendered.size() >= 1, 1500);
        QCOMPARE(preview.markdown(), QString("# Unsaved final\n"));
        QVERIFY(preview.renderedDocument()->toPlainText().contains("Unsaved final"));
        QVERIFY(!preview.renderedDocument()->toPlainText().contains("Saved copy"));
        QFile onDisk(source);
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        QCOMPARE(onDisk.readAll(), QByteArray("# Saved copy\n"));
        const QString before = preview.renderedDocument()->toPlainText();
        preview.setPalette(options(true).palette);
        preview.setDocumentFont(QFont(QStringLiteral("Arial"), 16));
        preview.setCodeFont(QFont(QStringLiteral("Consolas"), 13));
        preview.setZoomPercent(150);
        preview.renderNow();
        QCOMPARE(preview.renderedDocument()->toPlainText(), before);
        QCOMPARE(preview.zoomPercent(), 150);
        QCOMPARE(preview.textFont().pointSize(), 16);
        QCOMPARE(preview.codeFont().pointSize(), 13);
    }

    void oneLightContrastSurvivesEightBitColorQuantization()
    {
        auto value = options();
        value.palette.setColor(QPalette::Base, QColor("#fafafa"));
        value.palette.setColor(QPalette::Text, QColor("#383a42"));
        value.syntaxColors = {QColor("#a626a4"), QColor("#507b24"), QColor("#986801"), QColor("#696c77"), QColor("#986801")};
        MarkdownDocument document({});
        MarkdownRenderer::render(&document, "```python\n# Theme-aware code\nclass Document:\n    def render(self):\n        return \"Ready\", 42\n```\n\n> Readable quotation.\n", value);
        const auto luminance = [](const QColor &color) {
            const auto channel = [](int value) { const double x = value / 255.; return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4); };
            return .2126 * channel(color.red()) + .7152 * channel(color.green()) + .0722 * channel(color.blue());
        };
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            const auto background = block.blockFormat().background();
            const QColor paper = background.style() == Qt::NoBrush ? value.palette.color(QPalette::Base) : background.color();
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                if (fragment.text().trimmed().isEmpty()) continue;
                const QColor ink = fragment.charFormat().foreground().color();
                const double first = luminance(ink), second = luminance(paper);
                const double ratio = (std::max(first, second) + .05) / (std::min(first, second) + .05);
                QVERIFY2(ratio >= 4.5, qPrintable(QString("%1: %2 on %3 = %4").arg(fragment.text(), ink.name(), paper.name()).arg(ratio)));
            }
        }
    }

    void localLinksEmitSourceNavigationAndImagesDoNotLeakAcrossFiles()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("first"));
        QVERIFY(QDir(directory.path()).mkdir("second"));
        const QString first = directory.path() + "/first";
        const QString second = directory.path() + "/second";
        writeFile(first + "/target.md", "# Target\n");
        writeFile(directory.path() + "/outside.md", "# Outside\n");
        QImage image(12, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(first + "/image.png"));
        image.fill(Qt::blue);
        QVERIFY(image.save(second + "/image.png"));
        MarkdownPreview preview;
        preview.setMarkdown("# One\n\n![Image](image.png)\n", first + "/source.md");
        preview.renderNow();
        auto *browser = preview.findChild<QTextBrowser *>(QStringLiteral("MarkdownPreviewText"));
        QVERIFY(browser);
        QSignalSpy navigation(&preview, &MarkdownPreview::openSourceRequested);
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Q_ARG(QUrl, QUrl("target.md"))));
        QCOMPARE(navigation.size(), 1);
        QVERIFY(navigation.first().at(0).toString().endsWith("/first/target.md"));
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Q_ARG(QUrl, QUrl("../outside.md"))));
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Q_ARG(QUrl, QUrl("javascript:alert(1)"))));
        QCOMPARE(navigation.size(), 1);
        const QImage before = qvariant_cast<QImage>(preview.renderedDocument()->resource(QTextDocument::ImageResource, QUrl("image.png")));
        QCOMPARE(before.pixelColor(0, 0), QColor(Qt::red));
        preview.setMarkdown("# Two\n\n![Image](image.png)\n", second + "/source.md");
        preview.renderNow();
        const QImage after = qvariant_cast<QImage>(preview.renderedDocument()->resource(QTextDocument::ImageResource, QUrl("image.png")));
        QCOMPARE(after.pixelColor(0, 0), QColor(Qt::blue));
    }

    void markdownExtensions()
    {
        QVERIFY(MarkdownRenderer::isMarkdownPath("README.MD"));
        QVERIFY(MarkdownRenderer::isMarkdownPath("notes.markdown"));
        QVERIFY(!MarkdownRenderer::isMarkdownPath("notes.md.exe"));
    }

    void renderExamplesForVisualReview()
    {
        const QString output = qEnvironmentVariable("BIT8PAD_MARKDOWN_SCREENSHOTS");
        if (output.isEmpty()) return;
        QVERIFY(QDir().mkpath(output));
        const QString source = QStringLiteral(
            "# 8bitPad\n\nA fast, familiar editor for **Windows and Linux**. This preview follows your theme.\n\n"
            "## Getting started\n\n"
            "- [x] Create a file with your preferred extension\n"
            "- [x] Edit Markdown and see changes immediately\n"
            "- [ ] Ship the next release\n\n"
            "> Keep the source readable. Preview prose, tables and code without leaving the editor.\n\n"
            "## Build matrix\n\n"
            "| Platform | Toolchain | Status |\n| --- | --- | --- |\n"
            "| Windows | MSVC + Qt 6 | Ready |\n| Linux | GCC + Qt 6 | Supported |\n\n"
            "## Example\n\n"
            "```python\nfrom pathlib import Path\n\ndef load_document(path):\n    # A small, readable example\n    text = Path(path).read_text(encoding=\"utf-8\")\n    return {\"content\": text, \"lines\": 42}\n```\n\n"
            "Use `Ctrl+Shift+M` to open the preview. Visit the [project documentation](https://example.com/docs).\n");
        for (bool dark : {false, true}) {
            MarkdownPreview preview;
            preview.resize(880, 980);
            auto palette = options(dark).palette;
            palette.setColor(QPalette::Window, dark ? QColor("#141922") : QColor("#f4f5f7"));
            palette.setColor(QPalette::Button, palette.color(QPalette::Window));
            palette.setColor(QPalette::WindowText, palette.color(QPalette::Text));
            palette.setColor(QPalette::ButtonText, palette.color(QPalette::Text));
            preview.setPalette(palette);
            preview.setMarkdown(source, {});
            preview.show();
            preview.renderNow();
            QTest::qWait(80);
            QVERIFY(preview.grab().save(output + (dark ? "/markdown-dark.png" : "/markdown-light.png")));
        }
    }
};

QTEST_MAIN(MarkdownPreviewTest)
#include "MarkdownPreviewTest.moc"
