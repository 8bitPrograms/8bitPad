/* Copyright 2026 8bitPad contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "FunctionExtractor.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
template<class Callback> void lines(QStringView text, Callback callback, const FunctionExtractor::CancelCheck &cancelled = {})
{
    qsizetype start = 0;
    int line = 0;
    for (qsizetype position = 0; position < text.size(); ++position) {
        if (cancelled && position % 1024 == 0 && cancelled()) return;
        if (text.at(position) != QLatin1Char('\n') && text.at(position) != QLatin1Char('\r')) continue;
        callback(text.mid(start, position - start), line++);
        if (text.at(position) == QLatin1Char('\r') && position + 1 < text.size() && text.at(position + 1) == QLatin1Char('\n')) ++position;
        start = position + 1;
    }
    callback(text.mid(start), line);
}

// Strip comments and quoted text before matching declarations. Carry multiline
// comment/triple-quote state between lines, so a commented-out function is not
// offered as a navigation target. Full language grammar remains out of scope.
class Sanitizer {
public:
    explicit Sanitizer(QString value) : language(std::move(value)) {}
    QString clean(QStringView line, const FunctionExtractor::CancelCheck &cancelled)
    {
        QString result(std::min<qsizetype>(line.size(), 4096), QLatin1Char(' '));
        QChar quote;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (cancelled && i % 1024 == 0 && cancelled()) break;
            const auto rest = line.mid(i);
            if (!closing.isEmpty()) {
                if (rest.startsWith(closing)) { i += closing.size() - 1; closing.clear(); }
                continue;
            }
            if (!quote.isNull()) {
                if (line.at(i) == QLatin1Char('\\')) ++i;
                else if (line.at(i) == quote) quote = QChar();
                continue;
            }
            if (language == QLatin1String("python") && (rest.startsWith(u"\"\"\"") || rest.startsWith(u"'''"))) {
                closing = rest.left(3).toString(); i += 2; continue;
            }
            if (language == QLatin1String("lua") && rest.startsWith(u"--[[")) {
                closing = QStringLiteral("]]"); i += 3; continue;
            }
            if (rest.startsWith(u"/*") && language != QLatin1String("python") && language != QLatin1String("shell")) {
                closing = QStringLiteral("*/"); ++i; continue;
            }
            const bool hashComments = language == QLatin1String("python") || language == QLatin1String("shell") || language == QLatin1String("ruby") || language == QLatin1String("powershell");
            if ((hashComments && line.at(i) == QLatin1Char('#')) ||
                (language == QLatin1String("lua") && rest.startsWith(u"--")) ||
                (!hashComments && language != QLatin1String("lua") && rest.startsWith(u"//"))) break;
            if (line.at(i) == QLatin1Char('"') || line.at(i) == QLatin1Char('\'') || line.at(i) == QLatin1Char('`')) {
                // Rust lifetimes are not quoted character literals.
                if (language == QLatin1String("rust") && line.at(i) == QLatin1Char('\'') &&
                    (i + 2 >= line.size() || line.at(i + 2) != QLatin1Char('\''))) {
                    if (i < result.size()) result[i] = line.at(i);
                    continue;
                }
                quote = line.at(i); continue;
            }
            if (i < result.size()) result[i] = line.at(i);
        }
        return result.trimmed();
    }
private:
    QString language;
    QString closing;
};

struct Pattern {
    QRegularExpression regex;
    QString kind;
    int nameGroup = 1;
    bool needsBody = false;
};

QVector<Pattern> patternsFor(const QString &language)
{
    QVector<Pattern> patterns;
    const auto add = [&patterns](const QString &pattern, const QString &kind, int group = 1, bool body = false) {
        patterns.append({QRegularExpression(pattern, QRegularExpression::UseUnicodePropertiesOption), kind, group, body});
    };
    if (language == QLatin1String("python")) {
        add(QStringLiteral(R"(^(?:async\s+)?def\s+([\w]+)\s*\()"), QStringLiteral("function"));
        add(QStringLiteral(R"(^class\s+([\w]+)\s*[:(])"), QStringLiteral("class"));
    } else if (language == QLatin1String("rust")) {
        add(QStringLiteral(R"(^(?:pub(?:\([^)]*\))?\s+)?(?:(?:async|unsafe|const|extern)\s+)*fn\s+([\w]+)\s*[<(])"), QStringLiteral("function"));
        add(QStringLiteral(R"(^(?:pub(?:\([^)]*\))?\s+)?(?:struct|enum|trait|mod)\s+([\w]+))"), QStringLiteral("type"));
    } else if (language == QLatin1String("go")) {
        add(QStringLiteral(R"(^func\s+(?:\([^)]*\)\s*)?([\w]+)\s*(?:\[[^]]*\])?\s*\()"), QStringLiteral("function"));
        add(QStringLiteral(R"(^type\s+([\w]+)\s+(?:struct|interface)\b)"), QStringLiteral("type"));
    } else if (language == QLatin1String("lua")) {
        add(QStringLiteral(R"(^(?:local\s+)?function\s+([\w.:]+)\s*\()"), QStringLiteral("function"));
        add(QStringLiteral(R"(^(?:local\s+)?([\w.]+)\s*=\s*function\s*\()"), QStringLiteral("function"));
    } else if (language == QLatin1String("ruby")) {
        add(QStringLiteral(R"(^def\s+([\w.?!]+))"), QStringLiteral("function"));
        add(QStringLiteral(R"(^(?:class|module)\s+([\w:]+))"), QStringLiteral("type"));
    } else if (language == QLatin1String("shell")) {
        add(QStringLiteral(R"(^(?:function\s+)?([\w]+)\s*\(\s*\)\s*\{)"), QStringLiteral("function"));
        add(QStringLiteral(R"(^function\s+([\w]+)\s*\{)"), QStringLiteral("function"));
    } else if (language == QLatin1String("powershell")) {
        add(QStringLiteral(R"((?i)^(?:function|filter)\s+([\w:-]+))"), QStringLiteral("function"));
    } else if (language == QLatin1String("javascript") || language == QLatin1String("typescript") || language == QLatin1String("php")) {
        add(QStringLiteral(R"(^(?:(?:export|default|async|public|private|protected|static)\s+)*function\s*\*?\s*([\w$]+)\s*\()"), QStringLiteral("function"));
        add(QStringLiteral(R"(^(?:export\s+)?(?:const|let|var)\s+([\w$]+)\s*=\s*(?:async\s+)?(?:\([^)]*\)|[\w$]+)\s*(?::[^=]+)?=>)"), QStringLiteral("function"));
        add(QStringLiteral(R"(^(?:(?:export|default|abstract)\s+)*(?:class|interface|enum)\s+([\w$]+))"), QStringLiteral("type"));
        add(QStringLiteral(R"(^(?:(?:async|static|get|set|public|private|protected|readonly)\s+)*([\w$]+)\s*\([^;]*\)\s*(?::[^{}]+)?\{)"), QStringLiteral("method"));
    } else if (language == QLatin1String("cpp") || language == QLatin1String("csharp") || language == QLatin1String("java")) {
        add(QStringLiteral(R"(^(?:(?:public|private|protected|internal|sealed|abstract|static|partial|final)\s+)*(?:class|struct|enum|interface|namespace|record)\s+([\w:]+))"), QStringLiteral("type"));
        add(QStringLiteral(R"(^(?:(?:[\w:~<>*&\[\],]+)\s+)+([\w:~]+)\s*\([^;{}]*\)\s*(?:[^;{}]*)$)"), QStringLiteral("function"), 1, true);
        add(QStringLiteral(R"(^([\w:~]+)\s*\([^;{}]*\)\s*(?:[^;{}]*)$)"), QStringLiteral("method"), 1, true);
    }
    return patterns;
}
}

QString FunctionExtractor::languageFor(const QString &language, const QString &fileName)
{
    QString value = language.toLower();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (value.contains("python") || extension == "py") return QStringLiteral("python");
    if (value.contains("typescript") || extension == "ts" || extension == "tsx") return QStringLiteral("typescript");
    if (value.contains("javascript") || value == "js" || extension == "js" || extension == "jsx" || extension == "mjs") return QStringLiteral("javascript");
    if (value == "c#" || value.contains("csharp") || extension == "cs") return QStringLiteral("csharp");
    if (value == "java" || extension == "java") return QStringLiteral("java");
    if (value == "rust" || extension == "rs") return QStringLiteral("rust");
    if (value == "go" || extension == "go") return QStringLiteral("go");
    if (value == "lua" || extension == "lua") return QStringLiteral("lua");
    if (value == "ruby" || extension == "rb") return QStringLiteral("ruby");
    if (value.contains("powershell") || extension == "ps1") return QStringLiteral("powershell");
    if (value == "bash" || value.contains("shell") || extension == "sh" || extension == "bash") return QStringLiteral("shell");
    if (value == "php" || extension == "php") return QStringLiteral("php");
    if (value.contains("c++") || value == "c" || value == "cpp" ||
        QSet<QString>{"c", "cpp", "cxx", "cc", "h", "hpp", "hxx"}.contains(extension)) return QStringLiteral("cpp");
    return {};
}

QVector<NavigationSymbol> FunctionExtractor::extract(const QByteArray &utf8, const QString &language, const QString &fileName, const CancelCheck &cancelled)
{
    QVector<NavigationSymbol> result;
    if (utf8.size() > MaximumDocumentBytes || (cancelled && cancelled())) return result;
    const QString family = languageFor(language, fileName);
    if (family.isEmpty()) return result;
    const QVector<Pattern> patterns = patternsFor(family);
    const QString text = QString::fromUtf8(utf8);
    Sanitizer sanitizer(family);
    QString pendingSignature;
    int pendingLine = -1;
    const QSet<QString> controls {"if", "for", "while", "switch", "catch", "return", "sizeof", "delete", "new", "throw", "else", "using", "lock"};
    const auto match = [&](QString source, int line, bool body) {
        // C-like signatures are matched without the body, but only accepted if
        // the same line or immediately following line opens a body.
        const QString original = source;
        const qsizetype brace = source.indexOf(QLatin1Char('{'));
        if (brace >= 0) { body = true; source.truncate(brace); source = source.trimmed(); }
        for (const Pattern &pattern : patterns) {
            if (pattern.needsBody && !body) continue;
            const QString &candidate = pattern.needsBody ? source : original;
            const auto found = pattern.regex.match(candidate);
            if (!found.hasMatch()) continue;
            const QString name = found.captured(pattern.nameGroup);
            if (controls.contains(name)) continue;
            if (result.size() < MaximumSymbols) result.append({name, pattern.kind, line});
            return true;
        }
        return false;
    };
    lines(QStringView(text), [&](QStringView line, int number) {
        if (result.size() >= MaximumSymbols) return;
        // Limit the subject length passed to regexes even for minified files.
        const QString clean = sanitizer.clean(line, cancelled);
        if (cancelled && cancelled()) return;
        if (clean.isEmpty()) return;
        if (!pendingSignature.isEmpty() && clean.startsWith(QLatin1Char('{')))
            match(pendingSignature, pendingLine, true);
        pendingSignature.clear();
        if (!match(clean, number, false) && clean.contains(QLatin1Char('(')) && !clean.contains(QLatin1Char(';')) && clean.endsWith(QLatin1Char(')'))) {
            pendingSignature = clean;
            pendingLine = number;
        } else if ((family == "cpp" || family == "csharp" || family == "java") && !clean.contains(QLatin1Char(';')) &&
                   clean.contains(QLatin1Char(')')) && !clean.contains(QLatin1Char('{'))) {
            pendingSignature = clean;
            pendingLine = number;
        }
    }, cancelled);
    if (cancelled && cancelled()) result.clear();
    return result;
}

DocumentOverview FunctionExtractor::overview(const QByteArray &utf8, const CancelCheck &cancelled)
{
    DocumentOverview result;
    if (utf8.size() > MaximumDocumentBytes || (cancelled && cancelled())) return result;
    const QString text = QString::fromUtf8(utf8);
    lines(QStringView(text), [&](QStringView, int line) { result.lineCount = line + 1; }, cancelled);
    if (cancelled && cancelled()) return {};
    const int rowCount = std::min(result.lineCount, MaximumOverviewRows);
    result.rows.resize(rowCount);
    lines(QStringView(text), [&](QStringView line, int number) {
        int first = -1;
        int last = 0;
        int column = 0;
        for (QChar character : line.left(512)) {
            if (!character.isSpace()) { if (first < 0) first = column; last = column + 1; }
            column += character == QLatin1Char('\t') ? 4 - column % 4 : 1;
        }
        if (first < 0) return;
        const int row = int(qint64(number) * rowCount / result.lineCount);
        OverviewSpan &span = result.rows[row];
        const int cappedStart = std::min(first, 119);
        const int cappedEnd = std::min(std::max(last, cappedStart + 1), 120);
        if (span.width == 0) { span.indentation = cappedStart; span.width = cappedEnd - cappedStart; }
        else {
            const int right = std::max(span.indentation + span.width, cappedEnd);
            span.indentation = std::min(span.indentation, cappedStart);
            span.width = right - span.indentation;
        }
    }, cancelled);
    if (cancelled && cancelled()) return {};
    return result;
}
