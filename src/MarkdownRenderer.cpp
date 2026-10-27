// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 8bitPad contributors
#include "MarkdownRenderer.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <algorithm>
#include <cmath>

namespace {

QColor mix(const QColor &a, const QColor &b, qreal weight)
{
    // Measure the same 8-bit colors that the desktop actually paints. Keeping
    // fractional channels can pass 4.5 before quantization but fall below it
    // afterward (notably One Light's green strings on the code surface).
    return QColor(qRound(a.red() * (1 - weight) + b.red() * weight),
                  qRound(a.green() * (1 - weight) + b.green() * weight),
                  qRound(a.blue() * (1 - weight) + b.blue() * weight));
}

struct Colors
{
    QColor background, foreground, muted, surface, border, link;
    QColor keyword, string, number, comment, type;
};

qreal luminance(const QColor &color)
{
    const auto linear = [](qreal channel) { return channel <= .04045 ? channel / 12.92 : std::pow((channel + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

QColor readable(QColor color, const QColor &background)
{
    const qreal base = luminance(background);
    const auto contrast = [base](const QColor &candidate) {
        const qreal value = luminance(candidate);
        return (std::max(value, base) + .05) / (std::min(value, base) + .05);
    };
    if (contrast(color) >= 4.5) return color;
    const QColor target = contrast(Qt::white) > contrast(Qt::black) ? QColor(Qt::white) : QColor(Qt::black);
    for (int step = 1; step <= 100; ++step) {
        const QColor candidate = mix(color, target, step / 100.);
        if (contrast(candidate) >= 4.5) return candidate;
    }
    return target;
}

Colors colorsFor(const MarkdownRenderOptions &options)
{
    const QPalette &palette = options.palette;
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    const bool dark = base.lightnessF() < 0.45;
    const QColor surface = mix(base, text, .055);
    const auto token = [&surface, dark](const QColor &preferred, const char *darkDefault, const char *lightDefault) {
        return readable(preferred.isValid() ? preferred : QColor(dark ? darkDefault : lightDefault), surface);
    };
    return {base, readable(text, surface), readable(mix(text, base, .38), surface), surface,
            mix(base, text, .20), readable(palette.color(QPalette::Link), base),
            token(options.syntaxColors.keyword, "#d2a8ff", "#8250df"),
            token(options.syntaxColors.string, "#a5d6a7", "#116329"),
            token(options.syntaxColors.number, "#ffca80", "#953800"),
            token(options.syntaxColors.comment, "#8b99aa", "#57606a"),
            token(options.syntaxColors.type, "#79d9e8", "#055d68")};
}

bool isCodeBlock(const QTextBlock &block)
{
    const QTextBlockFormat format = block.blockFormat();
    return format.hasProperty(QTextFormat::BlockCodeFence)
        || format.hasProperty(QTextFormat::BlockCodeLanguage);
}

void colorRange(QTextDocument *document, int position, int length, const QColor &color)
{
    if (length <= 0) return;
    QTextCursor cursor(document);
    cursor.setPosition(position);
    cursor.setPosition(position + length, QTextCursor::KeepAnchor);
    QTextCharFormat format;
    format.setForeground(color);
    cursor.mergeCharFormat(format);
}

// A small, bounded lexer for preview code blocks. The source editor continues
// to use Lexilla. This has no execution or language-server dependencies.
void highlightCode(QTextDocument *document, const QTextBlock &block,
                   const QString &language, const Colors &colors,
                   QString &continuedString, bool &blockComment)
{
    const QString lang = language.toLower().section(QRegularExpression("\\s+"), 0, 0);
    const bool python = lang == "python" || lang == "py";
    const bool ruby = lang == "ruby" || lang == "rb";
    const bool shell = lang == "bash" || lang == "sh" || lang == "shell" || lang == "zsh";
    const bool powershell = lang == "powershell" || lang == "ps1" || lang == "pwsh";
    const bool yaml = lang == "yaml" || lang == "yml";
    const bool json = lang == "json" || lang == "jsonc";
    const bool sql = lang == "sql";
    const bool lua = lang == "lua";
    const bool markup = lang == "html" || lang == "xml" || lang == "svg";
    static const QSet<QString> cLanguages = {"c", "cpp", "c++", "cxx", "h", "hpp", "cs", "csharp",
        "c#", "java", "javascript", "js", "jsx", "typescript", "ts", "tsx", "go", "rust", "rs",
        "css", "scss", "kotlin", "swift"};
    const bool cLike = cLanguages.contains(lang);
    if (!(python || ruby || shell || powershell || yaml || json || sql || lua || markup || cLike)) return;

    static const QSet<QString> keywords = [] {
        return QSet<QString>{"as", "async", "await", "auto", "bool", "break", "case", "catch", "char", "class",
            "const", "continue", "def", "del", "do", "double", "elif", "else", "elseif", "end", "enum", "except",
            "export", "extends", "extern", "false", "finally", "float", "fn", "for", "foreach", "from", "func",
            "function", "global", "if", "impl", "import", "in", "include", "int", "interface", "is", "lambda",
            "let", "local", "long", "match", "mod", "mut", "namespace", "new", "nil", "none", "not", "null",
            "nullptr", "or", "and", "override", "package", "pass", "private", "protected", "pub", "public",
            "raise", "ref", "return", "self", "short", "static", "string", "struct", "super", "switch", "then",
            "this", "throw", "trait", "true", "try", "type", "typedef", "typeof", "undefined", "union", "unsafe",
            "unsigned", "use", "using", "val", "var", "virtual", "void", "volatile", "when", "while", "with", "yield",
            "select", "insert", "update", "delete", "create", "table", "alter", "where", "join", "left", "right",
            "inner", "outer", "on", "into", "values", "order", "by", "group", "having", "limit", "distinct",
            "fi", "done", "esac", "echo", "exit", "param", "process", "begin"};
    }();
    const QString text = block.text();
    const int base = block.position();
    const bool hashComment = python || ruby || shell || powershell || yaml;
    int i = 0;
    while (i < text.size()) {
        const int start = i;
        if (blockComment) {
            const QString endToken = markup ? QStringLiteral("-->") : QStringLiteral("*/");
            const int end = text.indexOf(endToken, i);
            i = end < 0 ? text.size() : end + endToken.size();
            colorRange(document, base + start, i - start, colors.comment);
            blockComment = end < 0;
            continue;
        }
        if (!continuedString.isEmpty()) {
            const int end = text.indexOf(continuedString, i);
            i = end < 0 ? text.size() : end + continuedString.size();
            colorRange(document, base + start, i - start, colors.string);
            if (end >= 0) continuedString.clear();
            continue;
        }
        if ((hashComment && text.at(i) == '#') || ((cLike || json) && text.mid(i, 2) == "//")
                || ((sql || lua) && text.mid(i, 2) == "--")) {
            colorRange(document, base + i, text.size() - i, colors.comment);
            break;
        }
        if ((cLike && text.mid(i, 2) == "/*") || (markup && text.mid(i, 4) == "<!--")) {
            const QString endToken = markup ? QStringLiteral("-->") : QStringLiteral("*/");
            const int end = text.indexOf(endToken, i + (markup ? 4 : 2));
            i = end < 0 ? text.size() : end + endToken.size();
            colorRange(document, base + start, i - start, colors.comment);
            blockComment = end < 0;
            continue;
        }
        if (text.at(i) == '\'' || text.at(i) == '"' || (cLike && text.at(i) == '`')) {
            const QChar quote = text.at(i);
            if (python && text.mid(i, 3) == QString(3, quote)) {
                const QString token(3, quote);
                const int end = text.indexOf(token, i + 3);
                i = end < 0 ? text.size() : end + 3;
                if (end < 0) continuedString = token;
            } else {
                ++i;
                while (i < text.size()) {
                    if (text.at(i) == '\\') { i = std::min(i + 2, int(text.size())); continue; }
                    if (text.at(i++) == quote) break;
                }
            }
            int after = i;
            while (after < text.size() && text.at(after).isSpace()) ++after;
            colorRange(document, base + start, i - start,
                       json && after < text.size() && text.at(after) == ':' ? colors.type : colors.string);
            continue;
        }
        if (text.at(i).isDigit() && (i == 0 || (!text.at(i - 1).isLetterOrNumber() && text.at(i - 1) != '_'))) {
            ++i;
            while (i < text.size() && (text.at(i).isLetterOrNumber() || text.at(i) == '.' || text.at(i) == '_')) ++i;
            colorRange(document, base + start, i - start, colors.number);
            continue;
        }
        if (text.at(i).isLetter() || text.at(i) == '_' || ((shell || powershell) && text.at(i) == '$')) {
            ++i;
            while (i < text.size() && (text.at(i).isLetterOrNumber() || text.at(i) == '_' || (yaml && text.at(i) == '-'))) ++i;
            const QString word = text.mid(start, i - start);
            int after = i;
            while (after < text.size() && text.at(after).isSpace()) ++after;
            if ((yaml && after < text.size() && text.at(after) == ':') || word.startsWith('$')
                    || (markup && start > 0 && (text.at(start - 1) == '<' || text.at(start - 1) == '/')))
                colorRange(document, base + start, i - start, colors.type);
            else if (keywords.contains(word.toLower()))
                colorRange(document, base + start, i - start, colors.keyword);
            continue;
        }
        ++i;
    }
}

QList<int> headingLines(const QString &markdown)
{
    QList<int> result;
    const QStringList lines = markdown.split('\n');
    QChar fence;
    int fenceLength = 0;
    static const QRegularExpression startFence(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    static const QRegularExpression atx(QStringLiteral("^ {0,3}#{1,6}(?:\\s+|$)"));
    static const QRegularExpression setext(QStringLiteral("^ {0,3}(?:=+|-+)\\s*$"));
    for (int line = 0; line < lines.size(); ++line) {
        QString value = lines.at(line);
        // Block quotes can contain real Markdown headings too.
        value.remove(QRegularExpression(QStringLiteral("^(?: {0,3}> ?)+")));
        const auto match = startFence.match(value);
        if (match.hasMatch()) {
            const QString token = match.captured(1);
            if (fence.isNull()) { fence = token.at(0); fenceLength = token.size(); }
            else if (token.at(0) == fence && token.size() >= fenceLength
                     && value.mid(match.capturedEnd()).trimmed().isEmpty()) fence = {};
            continue;
        }
        if (!fence.isNull()) continue;
        if (atx.match(value).hasMatch()) result.append(line);
        else if (line > 0 && setext.match(value).hasMatch() && !lines.at(line - 1).trimmed().isEmpty()
                 && !atx.match(lines.at(line - 1)).hasMatch()) result.append(line - 1);
    }
    return result;
}

void styleTables(QTextFrame *frame, const Colors &colors)
{
    for (QTextFrame *child : frame->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(child)) {
            QTextTableFormat format = table->format();
            format.setBorder(1);
            format.setBorderBrush(colors.border);
            format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            format.setCellPadding(9);
            format.setCellSpacing(0);
            format.setTopMargin(10);
            format.setBottomMargin(14);
            format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
            table->setFormat(format);
            for (int row = 0; row < table->rows(); ++row) {
                for (int col = 0; col < table->columns(); ++col) {
                    auto cell = table->cellAt(row, col);
                    auto cellFormat = cell.format().toTableCellFormat();
                    cellFormat.setBorder(0.5);
                    cellFormat.setBorderBrush(colors.border);
                    cellFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                    cellFormat.setBackground(row == 0 || row % 2 == 0 ? colors.surface : colors.background);
                    cell.setFormat(cellFormat);
                }
            }
        }
        styleTables(child, colors);
    }
}

void frameCodeBlocks(QTextDocument *document, const Colors &colors)
{
    struct Range { int first, last; QTextBlockFormat firstFormat; };
    QList<Range> ranges;
    for (QTextBlock block = document->begin(); block.isValid();) {
        if (!isCodeBlock(block)) { block = block.next(); continue; }
        const int first = block.position();
        const QTextBlockFormat firstFormat = block.blockFormat();
        int last = first;
        do {
            last = block.position() + block.length() - 1;
            block = block.next();
        } while (block.isValid() && isCodeBlock(block));
        ranges.append({first, last, firstFormat});
    }
    // A real QTextFrame paints the full panel, including the space between
    // lines. Block backgrounds alone leave stripes when line height is >100%.
    for (auto it = ranges.crbegin(); it != ranges.crend(); ++it) {
        QTextCursor cursor(document);
        cursor.setPosition(it->first);
        cursor.setPosition(it->last, QTextCursor::KeepAnchor);
        QTextFrameFormat frame;
        frame.setBackground(colors.surface);
        frame.setBorder(0.5);
        frame.setBorderBrush(colors.border);
        frame.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        frame.setPadding(12);
        frame.setTopMargin(8);
        frame.setBottomMargin(8);
        frame.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        QTextFrame *inserted = cursor.insertFrame(frame);
        QTextCursor firstCursor = inserted->firstCursorPosition();
        firstCursor.setBlockFormat(it->firstFormat);
        // Qt introduces an outer paragraph for a text frame. It can inherit
        // the selected code block's background; keep these structural spacers
        // small and unpainted instead of leaving a stray colored stripe.
        for (int position : {inserted->firstPosition() - 1, inserted->lastPosition() + 1}) {
            const QTextBlock spacer = document->findBlock(position);
            if (!spacer.isValid() || !spacer.text().isEmpty()) continue;
            QTextBlockFormat spacing;
            spacing.setLineHeight(1, QTextBlockFormat::FixedHeight);
            QTextCursor spacingCursor(spacer);
            spacingCursor.setBlockFormat(spacing);
        }
    }
}

QImage blockedImage()
{
    // Returning a valid image prevents QTextDocument's fallback resource
    // provider from trying to load a denied URL.
    QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

} // namespace

MarkdownDocument::MarkdownDocument(const QString &sourcePath, QObject *parent)
    : QTextDocument(parent), mSourcePath(sourcePath)
{
    if (!sourcePath.isEmpty()) {
        mResourceRoot = QFileInfo(QFileInfo(sourcePath).absolutePath()).canonicalFilePath();
        if (mResourceRoot.startsWith("//") || mResourceRoot.startsWith("\\\\")) mResourceRoot.clear();
    }
    if (!mResourceRoot.isEmpty()) setBaseUrl(QUrl::fromLocalFile(mResourceRoot + '/'));
}

QString MarkdownDocument::localFileWithinRoot(const QUrl &url, const QString &root)
{
    if (root.isEmpty() || !url.isValid() || url.hasQuery() || !url.host().isEmpty()
        || (!url.scheme().isEmpty() && url.scheme() != QStringLiteral("file"))) return {};
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty() || !QFileInfo(canonicalRoot).isDir()) return {};
    QString path = url.isLocalFile() ? url.toLocalFile() : url.path(QUrl::FullyDecoded);
    if (path.isEmpty() || path.startsWith("//") || path.startsWith("\\\\")) return {};
    if (!QDir::isAbsolutePath(path)) path = QDir(canonicalRoot).absoluteFilePath(path);
    const QFileInfo file(path);
    const QString canonicalFile = file.canonicalFilePath();
    if (!file.isFile() || canonicalFile.isEmpty()) return {};
    const QString relative = QDir(canonicalRoot).relativeFilePath(canonicalFile);
    if (QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../")) return {};
    return canonicalFile;
}

QVariant MarkdownDocument::loadResource(int type, const QUrl &name)
{
    if (type != QTextDocument::ImageResource) return QByteArray();
    const QString path = localFileWithinRoot(name, mResourceRoot);
    if (path.isEmpty()) return blockedImage();
    const QFileInfo file(path);
    static const QSet<QString> rasterFormats = {"png", "jpg", "jpeg", "gif", "bmp", "webp"};
    if (!rasterFormats.contains(file.suffix().toLower()) || file.size() > 16 * 1024 * 1024) return blockedImage();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || dimensions.width() > 16000 || dimensions.height() > 16000
            || qint64(dimensions.width()) * dimensions.height() > 32 * 1024 * 1024) return blockedImage();
    // Decode a reasonably sized preview instead of retaining a full photo.
    if (dimensions.width() > 1600 || dimensions.height() > 1600)
        reader.setScaledSize(dimensions.scaled(1600, 1600, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    return image.isNull() ? blockedImage() : image;
}

QList<MarkdownHeading> MarkdownRenderer::render(MarkdownDocument *document,
                                               const QString &markdown,
                                               const MarkdownRenderOptions &options)
{
    if (!document) return {};
    const Colors colors = colorsFor(options);
    QFont textFont = options.textFont;
    if (textFont.family().isEmpty()) textFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (textFont.pointSizeF() <= 0) textFont.setPointSizeF(11);
    QFont codeFont = options.codeFont;
    if (codeFont.family().isEmpty()) codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setFixedPitch(true);
    if (codeFont.pointSizeF() <= 0) codeFont.setPointSizeF(std::max(8., textFont.pointSizeF() - 1));
    document->setUndoRedoEnabled(false);
    document->setDefaultFont(textFont);
    document->setDocumentMargin(24);
    document->setDefaultStyleSheet(QStringLiteral("body { color: %1; } a { color: %2; } ")
                                  .arg(colors.foreground.name(), colors.link.name()));
    document->setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
                                   | QTextDocument::MarkdownNoHTML);
    QTextCursor all(document);
    all.select(QTextCursor::Document);
    QTextCharFormat defaultFormat;
    defaultFormat.setForeground(colors.foreground);
    all.mergeCharFormat(defaultFormat);
    auto rootFormat = document->rootFrame()->frameFormat();
    rootFormat.setBackground(colors.background);
    document->rootFrame()->setFrameFormat(rootFormat);

    QList<MarkdownHeading> headings;
    QHash<QString, int> slugCounts;
    const QList<int> sourceLines = headingLines(markdown);
    QString language, continuedString;
    bool blockComment = false;
    bool previousCode = false;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        QTextBlockFormat format = block.blockFormat();
        const int heading = format.headingLevel();
        const bool code = isCodeBlock(block);
        format.setTopMargin(0);
        format.setBottomMargin(10);
        format.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
        if (heading > 0) {
            const QString title = block.text();
            QString slug = title.toLower();
            slug.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_\\-\\s]")));
            slug.replace(QRegularExpression(QStringLiteral("\\s")), QStringLiteral("-"));
            if (slug.isEmpty()) slug = QStringLiteral("section");
            const int duplicate = slugCounts[slug]++;
            if (duplicate) slug += '-' + QString::number(duplicate);
            const int index = headings.size();
            headings.append({title, slug, heading, sourceLines.value(index, 0), block.position()});
            format.setTopMargin(index == 0 && block.position() == 0 ? 2 : 18);
            format.setBottomMargin(heading <= 2 ? 12 : 8);
            format.setLineHeight(120, QTextBlockFormat::ProportionalHeight);
            cursor.select(QTextCursor::BlockUnderCursor);
            QTextCharFormat headingFormat;
            const qreal ratios[] = {1, 1.95, 1.55, 1.28, 1.15, 1.05, 1.0};
            headingFormat.setFontPointSize(textFont.pointSizeF() * ratios[std::clamp(heading, 1, 6)]);
            headingFormat.setFontWeight(QFont::DemiBold);
            cursor.mergeCharFormat(headingFormat);
            cursor.setPosition(block.position());
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            QTextCharFormat anchor;
            anchor.setAnchorNames({slug});
            cursor.mergeCharFormat(anchor);
        } else if (code) {
            if (!previousCode) { language.clear(); continuedString.clear(); blockComment = false; }
            const QString ownLanguage = format.stringProperty(QTextFormat::BlockCodeLanguage);
            if (!ownLanguage.isEmpty()) language = ownLanguage;
            format.setBackground(colors.surface);
            format.setLeftMargin(0);
            format.setRightMargin(0);
            format.setTopMargin(0);
            format.setBottomMargin(0);
            format.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
            cursor.select(QTextCursor::BlockUnderCursor);
            QTextCharFormat codeFormat;
            codeFormat.setFont(codeFont);
            codeFormat.setForeground(colors.foreground);
            cursor.mergeCharFormat(codeFormat);
            highlightCode(document, block, language, colors, continuedString, blockComment);
        } else if (format.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
            format.setLeftMargin(18 * format.intProperty(QTextFormat::BlockQuoteLevel));
            format.setRightMargin(12);
            format.setBackground(colors.surface);
            cursor.select(QTextCursor::BlockUnderCursor);
            QTextCharFormat quote;
            quote.setForeground(colors.muted);
            cursor.mergeCharFormat(quote);
        } else if (block.textList()) {
            format.setBottomMargin(5);
        }
        // A character selection can straddle the preceding paragraph separator
        // (BlockUnderCursor does this for non-first blocks). Apply paragraph
        // presentation with a fresh, collapsed cursor so it never leaks into a
        // heading/list item immediately before a quote or fenced code block.
        QTextCursor blockCursor(block);
        blockCursor.setBlockFormat(format);
        previousCode = code;

        // QText fragments preserve Markdown emphasis, task markers and links.
        // Only presentation properties are amended here.
        struct FragmentSnapshot { int position, length; QTextCharFormat format; };
        QList<FragmentSnapshot> fragments;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid()) fragments.append({fragment.position(), fragment.length(), fragment.charFormat()});
        }
        for (const auto &fragment : fragments) {
            QTextCharFormat charFormat = fragment.format;
            QTextCursor fragmentCursor(document);
            fragmentCursor.setPosition(fragment.position);
            fragmentCursor.setPosition(fragment.position + fragment.length, QTextCursor::KeepAnchor);
            if (charFormat.isImageFormat()) {
                QTextImageFormat imageFormat = charFormat.toImageFormat();
                const QImage image = qvariant_cast<QImage>(document->resource(QTextDocument::ImageResource, QUrl(imageFormat.name())));
                if (image.width() > 1 && image.height() > 1) {
                    const qreal width = std::min(image.width(), std::max(80, options.contentWidth));
                    imageFormat.setWidth(width);
                    imageFormat.setHeight(width * image.height() / image.width());
                    fragmentCursor.setCharFormat(imageFormat);
                }
            } else if (!charFormat.anchorHref().isEmpty()) {
                QTextCharFormat link;
                link.setForeground(colors.link);
                link.setFontUnderline(true);
                fragmentCursor.mergeCharFormat(link);
            } else if (!code && (charFormat.fontFixedPitch()
                       || charFormat.fontFamilies().toStringList().contains(QStringLiteral("monospace")))) {
                QTextCharFormat inlineCode;
                inlineCode.setFont(codeFont);
                inlineCode.setBackground(colors.surface);
                fragmentCursor.mergeCharFormat(inlineCode);
            }
        }
    }
    styleTables(document->rootFrame(), colors);
    frameCodeBlocks(document, colors);
    // Frames introduce structural boundary characters. Refresh source-outline
    // positions after inserting them; named anchors and source lines persist.
    int headingIndex = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() > 0 && headingIndex < headings.size())
            headings[headingIndex++].documentPosition = block.position();
    }
    return headings;
}

bool MarkdownRenderer::isMarkdownPath(const QString &path)
{
    static const QSet<QString> extensions = {"md", "markdown", "mdown", "mkd", "mkdn"};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
