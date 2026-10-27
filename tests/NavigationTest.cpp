/* Copyright 2026 8bitPad contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "FunctionExtractor.h"
#include <QtTest>

class NavigationTest : public QObject
{
    Q_OBJECT
private slots:
    void detectsLanguageAndExtension()
    {
        QCOMPARE(FunctionExtractor::languageFor("C++", "untitled"), QString("cpp"));
        QCOMPARE(FunctionExtractor::languageFor("text", "worker.tsx"), QString("typescript"));
        QCOMPARE(FunctionExtractor::languageFor({}, "WORKER.PY"), QString("python"));
        QVERIFY(FunctionExtractor::languageFor("text", "readme.txt").isEmpty());
    }

    void pythonOutlineExcludesCommentsAndDocstrings()
    {
        const QByteArray source =
            "# def hidden():\n"
            "class Worker:\n"
            "    \"\"\"A multiline docstring.\n"
            "    def imaginary():\n"
            "    \"\"\"\n"
            "    async def fetch(self):\n"
            "        pass\n"
            "def main():\n"
            "    pass\n";
        const auto symbols = FunctionExtractor::extract(source, "Python");
        QCOMPARE(symbols.size(), qsizetype(3));
        QCOMPARE(symbols.at(0).name, QString("Worker"));
        QCOMPARE(symbols.at(0).line, 1);
        QCOMPARE(symbols.at(1).name, QString("fetch"));
        QCOMPARE(symbols.at(1).line, 5);
        QCOMPARE(symbols.at(2).name, QString("main"));
        QCOMPARE(symbols.at(2).line, 7);
    }

    void cppOutlineHandlesBodyOnNextLine()
    {
        const QByteArray source =
            "/*\nint imaginary() {\n}\n*/\n"
            "class Worker {\n};\n"
            "int Worker::parse() const\n{\n    if (true) {\n    }\n}\n"
            "void run() { call(); }\n"
            "void declaration();\n";
        const auto symbols = FunctionExtractor::extract(source, "C++");
        QCOMPARE(symbols.size(), qsizetype(3));
        QCOMPARE(symbols.at(0).name, QString("Worker"));
        QCOMPARE(symbols.at(1).name, QString("Worker::parse"));
        QCOMPARE(symbols.at(1).line, 6);
        QCOMPARE(symbols.at(2).name, QString("run"));
    }

    void javascriptFunctionsArrowFunctionsAndMethods()
    {
        const QByteArray source =
            "// function fake() {}\n"
            "export async function fetchData() { return 1; }\n"
            "const sum = (a, b) => a + b;\n"
            "export class Worker {\n"
            "  async run() { return 1; }\n"
            "  if (false) { ignored(); }\n"
            "}\n";
        const auto symbols = FunctionExtractor::extract(source, "JavaScript");
        QCOMPARE(symbols.size(), qsizetype(4));
        QCOMPARE(symbols.at(0).name, QString("fetchData"));
        QCOMPARE(symbols.at(1).name, QString("sum"));
        QCOMPARE(symbols.at(2).name, QString("Worker"));
        QCOMPARE(symbols.at(3).name, QString("run"));
    }

    void rustGoAndLuaOutline()
    {
        const auto rust = FunctionExtractor::extract("pub struct Worker {}\npub async fn fetch<'a>(s: &'a str) {}\n", "Rust");
        QCOMPARE(rust.size(), qsizetype(2));
        QCOMPARE(rust.at(1).name, QString("fetch"));
        const auto go = FunctionExtractor::extract("type Worker struct {}\nfunc (w *Worker) Run() {}\nfunc main() {}\n", "Go");
        QCOMPARE(go.size(), qsizetype(3));
        QCOMPARE(go.at(1).name, QString("Run"));
        const auto lua = FunctionExtractor::extract("--[[\nfunction imaginary()\n]]\nlocal function run()\nend\nM.fetch = function() end\n", "Lua");
        QCOMPARE(lua.size(), qsizetype(2));
        QCOMPARE(lua.at(0).name, QString("run"));
        QCOMPARE(lua.at(0).line, 3);
        QCOMPARE(lua.at(1).name, QString("M.fetch"));
    }

    void csharpJavaAndShellOutline()
    {
        const auto cs = FunctionExtractor::extract("public class Worker {\npublic static int Run() { return 1; }\n}\n", "C#");
        QCOMPARE(cs.size(), qsizetype(2));
        QCOMPARE(cs.at(1).name, QString("Run"));
        const auto java = FunctionExtractor::extract("public final class Worker {\npublic void run() { }\n}\n", "Java");
        QCOMPARE(java.size(), qsizetype(2));
        const auto shell = FunctionExtractor::extract("# fake() {\nrun() {\n echo ok\n}\nfunction deploy {\n}\n", "Bash");
        QCOMPARE(shell.size(), qsizetype(2));
        QCOMPARE(shell.at(1).name, QString("deploy"));
    }

    void unicodeNamesAndCrLfLines()
    {
        const auto symbols = FunctionExtractor::extract(QString::fromUtf8("# açıklama\r\ndef çağır():\r\n    pass\r\nclass Görev:\r\n    pass").toUtf8(), "Python");
        QCOMPARE(symbols.size(), qsizetype(2));
        QCOMPARE(symbols.at(0).name, QString::fromUtf8("çağır"));
        QCOMPARE(symbols.at(0).line, 1);
        QCOMPARE(symbols.at(1).line, 3);
    }

    void oldMacLineEndings()
    {
        const auto symbols = FunctionExtractor::extract("# comment\rdef run():\r    pass", "Python");
        QCOMPARE(symbols.size(), qsizetype(1));
        QCOMPARE(symbols.at(0).line, 1);
        QCOMPARE(FunctionExtractor::overview("one\rtwo\rthree").lineCount, 3);
    }

    void outlineCapBoundsResultSize()
    {
        QByteArray source;
        for (int i = 0; i < FunctionExtractor::MaximumSymbols + 5; ++i)
            source += "def f" + QByteArray::number(i) + "():\n  pass\n";
        const auto symbols = FunctionExtractor::extract(source, "Python");
        QCOMPARE(symbols.size(), qsizetype(FunctionExtractor::MaximumSymbols));
        QCOMPARE(symbols.last().name, QString("f4999"));
    }

    void longCommentLineDoesNotCorruptFollowingOutline()
    {
        QByteArray source = "/*" + QByteArray(5000, 'x') + "*/\nint run() { }\n";
        const auto symbols = FunctionExtractor::extract(source, "C++");
        QCOMPARE(symbols.size(), qsizetype(1));
        QCOMPARE(symbols.first().name, QString("run"));
        QCOMPARE(symbols.first().line, 1);
    }

    void overviewRetainsWhitespaceShape()
    {
        const auto overview = FunctionExtractor::overview("foo\n  bar\n\n\txy");
        QCOMPARE(overview.lineCount, 4);
        QCOMPARE(overview.rows.size(), qsizetype(4));
        QCOMPARE(overview.rows.at(0).indentation, 0);
        QCOMPARE(overview.rows.at(0).width, 3);
        QCOMPARE(overview.rows.at(1).indentation, 2);
        QCOMPARE(overview.rows.at(1).width, 3);
        QCOMPARE(overview.rows.at(2).width, 0);
        QCOMPARE(overview.rows.at(3).indentation, 4);
        QCOMPARE(overview.rows.at(3).width, 2);
    }

    void overviewCompressesVeryLongDocuments()
    {
        const auto overview = FunctionExtractor::overview(QByteArray("  abc\r\n").repeated(50000));
        QCOMPARE(overview.lineCount, 50001);
        QCOMPARE(overview.rows.size(), qsizetype(FunctionExtractor::MaximumOverviewRows));
        QCOMPARE(overview.rows.first().indentation, 2);
        QCOMPARE(overview.rows.first().width, 3);
    }

    void oversizedInputsAreRejectedBeforeParsing()
    {
        const QByteArray source(FunctionExtractor::MaximumDocumentBytes + 1, 'x');
        QVERIFY(FunctionExtractor::extract(source, "Python").isEmpty());
        QVERIFY(FunctionExtractor::overview(source).rows.isEmpty());
    }

    void cancellationDiscardsPartialResults()
    {
        const QByteArray source = QByteArray("def run():\n    pass\n").repeated(5000);
        int checks = 0;
        const auto cancel = [&checks] { return ++checks > 20; };
        QVERIFY(FunctionExtractor::extract(source, "Python", {}, cancel).isEmpty());
        checks = 0;
        QVERIFY(FunctionExtractor::overview(source, cancel).rows.isEmpty());
    }
};

QTEST_MAIN(NavigationTest)
#include "NavigationTest.moc"
