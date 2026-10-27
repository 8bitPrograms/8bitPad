#include "Finder.h"
#include "ScintillaNext.h"

#include <QFile>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QStringConverter>
#include <QTextCodec>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class EditorCoreTest : public QObject
{
    Q_OBJECT

    static QByteArray contents(ScintillaNext &editor)
    {
        return QByteArray(reinterpret_cast<const char *>(editor.characterPointer()), editor.length());
    }

    static bool writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

private slots:
    void unicodeRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("bom");
        QTest::addColumn<int>("encoding");
        QTest::newRow("utf8") << QByteArray() << int(QStringConverter::Utf8);
        QTest::newRow("utf8-bom") << QByteArray::fromHex("efbbbf") << int(QStringConverter::Utf8);
        QTest::newRow("utf16-le") << QByteArray::fromHex("fffe") << int(QStringConverter::Utf16LE);
        QTest::newRow("utf16-be") << QByteArray::fromHex("feff") << int(QStringConverter::Utf16BE);
    }

    void unicodeRoundTrip()
    {
        QFETCH(QByteArray, bom);
        QFETCH(int, encoding);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString text = QString::fromUtf8("Türkçe: ıİğĞşŞüÜöÖçÇ 🌍\r\n日本語\n");
        QStringEncoder encoder(static_cast<QStringConverter::Encoding>(encoding));
        const QByteArray source = bom + QByteArray(encoder(text));
        const QString input = directory.filePath("input.txt");
        const QString output = directory.filePath("output.txt");
        QVERIFY(writeFile(input, source));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(input));
        QVERIFY(editor);
        QCOMPARE(contents(*editor), text.toUtf8());
        QCOMPARE(editor->saveAs(output), QFileDevice::NoError);
        QFile result(output);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), source);
    }

    void utf16AcrossChunkBoundary_data()
    {
        QTest::addColumn<int>("encoding");
        QTest::addColumn<QByteArray>("bom");
        QTest::newRow("little-endian") << int(QStringConverter::Utf16LE) << QByteArray::fromHex("fffe");
        QTest::newRow("big-endian") << int(QStringConverter::Utf16BE) << QByteArray::fromHex("feff");
    }

    void utf16AcrossChunkBoundary()
    {
        QFETCH(int, encoding);
        QFETCH(QByteArray, bom);
        QTemporaryDir directory;
        // BOM + prefix + the first surrogate end exactly at the 4 MiB read boundary.
        const QString text = QString(2 * 1024 * 1024 - 2, QLatin1Char('x')) + QString::fromUtf8("🌍 Türkçe\n") + QString(400, QLatin1Char('y'));
        QStringEncoder encoder(static_cast<QStringConverter::Encoding>(encoding));
        const QByteArray source = bom + QByteArray(encoder(text));
        const QString path = directory.filePath("large.txt");
        QVERIFY(writeFile(path, source));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);
        QCOMPARE(contents(*editor), text.toUtf8());
        QCOMPARE(editor->save(), QFileDevice::NoError);
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), source);
    }

    void failedSaveRetainsDirtyDocument()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("document.txt");
        QVERIFY(writeFile(path, "original"));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);
        editor->setSavePoint();
        editor->setText("edited");
        QSignalSpy saved(editor.get(), &ScintillaNext::saved);
        QVERIFY(editor->saveAs(directory.filePath("missing/document.txt")) != QFileDevice::NoError);
        QCOMPARE(contents(*editor), QByteArray("edited"));
        QVERIFY(editor->canSaveToDisk());
        QCOMPARE(saved.count(), 0);
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original"));
    }

    void conversionFailureDoesNotTruncateOriginal()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("utf16.txt");
        const QByteArray original = QByteArray::fromHex("fffe61006200");
        QVERIFY(writeFile(path, original));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);
        editor->setText("\xff");
        QVERIFY(editor->save() != QFileDevice::NoError);
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), original);
    }

    void renameToSameFilePreservesDocument()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("document.txt");
        QVERIFY(writeFile(path, "original"));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);
        editor->setText("updated");
        QVERIFY(editor->rename(path));
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("updated"));
        QVERIFY(editor->isSavedToDisk());
    }

    void explorerMovePreservesDirtyBuffer()
    {
        QTemporaryDir directory;
        const QString oldDir = directory.filePath("old");
        const QString newDir = directory.filePath("new");
        QVERIFY(QDir().mkpath(oldDir));
        const QString oldPath = QDir(oldDir).filePath("test.py");
        const QString newPath = QDir(newDir).filePath("test.py");
        QVERIFY(writeFile(oldPath, "original"));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(oldPath));
        QVERIFY(editor);
        editor->setText("unsaved changes");
        QVERIFY(editor->canUndo());
        QSignalSpy saved(editor.get(), &ScintillaNext::saved);
        QSignalSpy renamed(editor.get(), &ScintillaNext::renamed);
        QVERIFY(QDir().rename(oldDir, newDir));
        QVERIFY(editor->followFileMove(oldDir, newDir));
        QCOMPARE(editor->getFileInfo().absoluteFilePath(), newPath);
        QCOMPARE(contents(*editor), QByteArray("unsaved changes"));
        QVERIFY(!editor->isSavedToDisk());
        QVERIFY(editor->canUndo());
        QCOMPARE(saved.count(), 0);
        QCOMPARE(renamed.count(), 1);
        QCOMPARE(editor->checkFileForStateChange(), ScintillaNext::NoChange);
        QFile disk(newPath);
        QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), QByteArray("original"));
        QVERIFY(!editor->followFileMove(directory.filePath("ne"), oldDir));
        QVERIFY(!editor->followFileMove(newPath, directory.filePath("missing.py")));
    }

    void failedReloadRetainsTextAndUndo()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("document.txt");
        QVERIFY(writeFile(path, "original"));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);
        editor->setSavePoint();
        editor->setText("unsaved changes");
        QVERIFY(editor->canUndo());
        // Invalid UTF-16 forces a decode failure after the file was successfully opened.
        QVERIFY(writeFile(path, QByteArray::fromHex("fffe6100ff")));
        QSignalSpy reloaded(editor.get(), &ScintillaNext::reloaded);
        editor->reload();
        QCOMPARE(contents(*editor), QByteArray("unsaved changes"));
        QVERIFY(editor->canUndo());
        QVERIFY(editor->canSaveToDisk());
        QCOMPARE(reloaded.count(), 0);
    }

    void zeroWidthReplaceProgresses_data()
    {
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("replacement");
        QTest::addColumn<QByteArray>("expected");
        QTest::addColumn<int>("count");
        QTest::newRow("lookahead-insert") << QByteArray("aba") << QString("(?=a)") << QString("_") << QByteArray("_ab_a") << 2;
        QTest::newRow("lookahead-empty") << QByteArray("aba") << QString("(?=a)") << QString() << QByteArray("aba") << 2;
        QTest::newRow("line-anchors") << QByteArray("a\nb") << QString("^") << QString(">") << QByteArray(">a\n>b") << 2;
        QTest::newRow("unicode") << QByteArray("ç🌍") << QString("(?=.)") << QString("_") << QByteArray("_ç_🌍") << 2;
    }

    void legacyEncodingRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("codecName");
        QTest::addColumn<QString>("text");
        QTest::newRow("turkish-cp1254") << QByteArray("windows-1254") << QString::fromUtf8("Türkçe ıİğĞşŞüÜöÖçÇ ve €\r\n");
        QTest::newRow("turkish-iso8859-9") << QByteArray("ISO-8859-9") << QString::fromUtf8("Türkiye ıİğĞşŞüÜöÖçÇ\n");
        QTest::newRow("western-cp1252") << QByteArray("windows-1252") << QString::fromUtf8("café — €\r\n");
        QTest::newRow("shift-jis") << QByteArray("Shift_JIS") << QString::fromUtf8("日本語の文章\n");
        QTest::newRow("gb18030") << QByteArray("GB18030") << QString::fromUtf8("中文文字\n");
    }

    void legacyEncodingRoundTrip()
    {
        QFETCH(QByteArray, codecName);
        QFETCH(QString, text);
        auto *codec = QTextCodec::codecForName(codecName);
        QVERIFY(codec);
        const QByteArray original = codec->fromUnicode(text);
        QTemporaryDir directory;
        const QString path = directory.filePath("legacy.txt");
        QVERIFY(writeFile(path, original));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path, false, QString::fromLatin1(codecName)));
        QVERIFY(editor);
        QCOMPARE(contents(*editor), text.toUtf8());
        QCOMPARE(editor->save(), QFileDevice::NoError);
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), original);
        result.close();
        QVERIFY(editor->setEncoding("UTF-8"));
        QCOMPARE(editor->save(), QFileDevice::NoError);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), text.toUtf8());
    }

    void gb18030NeverLosesSupplementaryCharacters()
    {
        const QString text = QString::fromUtf8("中文🌍\n");
        auto *codec = QTextCodec::codecForName("GB18030");
        QVERIFY(codec);
        const QByteArray original = codec->fromUnicode(text);
        QTemporaryDir directory;
        const QString path = directory.filePath("supplementary.txt");
        QVERIFY(writeFile(path, original));
        std::unique_ptr<ScintillaNext> loaded(ScintillaNext::fromFile(path, false, "GB18030"));
        // Qt versions differ: correct decoding is welcome, silent truncation is not.
        if (loaded) QCOMPARE(contents(*loaded), text.toUtf8());
        ScintillaNext editor("supplementary");
        editor.setCodePage(SC_CP_UTF8);
        editor.setText(text.toUtf8().constData());
        QVERIFY(editor.setEncoding("GB18030"));
        const auto status = editor.saveAs(path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        if (status == QFileDevice::NoError)
            QCOMPARE(codec->toUnicode(bytes), text);
        else {
            QCOMPARE(status, QFileDevice::WriteError);
            QCOMPARE(bytes, original);
            QVERIFY(editor.canSaveToDisk());
        }
    }

    void utf8PreferredAndLegacyDetection()
    {
        const QString text = QString::fromUtf8("Türkiye'de güzel bir yaz günü. Şimdi dışarıda güneş ışığı var. Çocuklar köprüden geçiyor.\r\n").repeated(30);
        QTemporaryDir directory;
        const QString utf8Path = directory.filePath("utf8.txt");
        QVERIFY(writeFile(utf8Path, text.toUtf8()));
        std::unique_ptr<ScintillaNext> utf8(ScintillaNext::fromFile(utf8Path));
        QVERIFY(utf8);
        QCOMPARE(utf8->encodingName(), QString("UTF-8"));
        const QString legacyPath = directory.filePath("turkish.txt");
        auto *codec = QTextCodec::codecForName("windows-1254");
        QVERIFY(codec);
        QVERIFY(writeFile(legacyPath, codec->fromUnicode(text)));
        std::unique_ptr<ScintillaNext> detected(ScintillaNext::fromFile(legacyPath));
        QVERIFY(detected);
        QCOMPARE(contents(*detected), text.toUtf8());
    }

    void unrepresentableSaveKeepsOriginalAndRecoveryText()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("latin.txt");
        QVERIFY(writeFile(path, "original"));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path));
        QVERIFY(editor);
        QVERIFY(editor->setEncoding("windows-1254"));
        const QByteArray edited = QString::fromUtf8("Türkçe 🌍").toUtf8();
        editor->setText(edited.constData());
        QVERIFY(editor->save() != QFileDevice::NoError);
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("original"));
        QVERIFY(editor->canSaveToDisk());
        const QString recovery = directory.filePath("recovery");
        QCOMPARE(editor->saveRecoveryCopyAs(recovery), QFileDevice::NoError);
        std::unique_ptr<ScintillaNext> restored(ScintillaNext::fromFile(recovery));
        QVERIFY(restored);
        QCOMPARE(contents(*restored), edited);
        QVERIFY(restored->setEncoding(editor->encodingName()));
        QCOMPARE(restored->encodingName(), editor->encodingName());
    }

    void explicitBomlessUtf16AndFailedReinterpretation()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("utf16.txt");
        const QByteArray original = QByteArray::fromHex("61006200");
        QVERIFY(writeFile(path, original));
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(path, false, "UTF-16LE (no BOM)"));
        QVERIFY(editor);
        QCOMPARE(contents(*editor), QByteArray("ab"));
        QCOMPARE(editor->encodingName(), QString("UTF-16LE (no BOM)"));
        QCOMPARE(editor->save(), QFileDevice::NoError);
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), original);
        result.close();
        QVERIFY(!editor->reloadWithEncoding("unknown-invalid-encoding"));
        QCOMPARE(contents(*editor), QByteArray("ab"));
        QVERIFY(editor->reloadWithEncoding("UTF-16LE (no BOM)"));
        QCOMPARE(contents(*editor), QByteArray("ab"));
    }

    void zeroWidthReplaceProgresses()
    {
        QFETCH(QByteArray, source);
        QFETCH(QString, expression);
        QFETCH(QString, replacement);
        QFETCH(QByteArray, expected);
        QFETCH(int, count);
        ScintillaNext editor("regex");
        editor.setCodePage(SC_CP_UTF8);
        editor.setText(source.constData());
        editor.emptyUndoBuffer();
        Finder finder(&editor);
        finder.options().text = expression;
        finder.options().flags = Scintilla::FindOption::RegExp;
        QCOMPARE(finder.count(), count);
        QCOMPARE(finder.replaceAll(replacement), count);
        QCOMPARE(contents(editor), expected);
        if (expected != source) editor.undo();
        else QVERIFY(!editor.canUndo());
        QCOMPARE(contents(editor), source);
    }

    void regexReplaceCapturesAndRangePreservesOutside()
    {
        ScintillaNext editor("range");
        editor.setCodePage(SC_CP_UTF8);
        const QByteArray source = QString::fromUtf8("🌍 outside: a1\ninside: a2 a3\noutside: a4").toUtf8();
        editor.setText(source.constData());
        editor.emptyUndoBuffer();
        Finder finder(&editor);
        finder.options().text = "a([0-9])";
        finder.options().flags = Scintilla::FindOption::RegExp;
        const auto start = editor.positionFromLine(1);
        const auto end = editor.lineEndPosition(1);
        QCOMPARE(finder.replaceAllInRange("value\\1", {static_cast<Sci_PositionCR>(start), static_cast<Sci_PositionCR>(end)}), 2);
        QCOMPARE(contents(editor), QString::fromUtf8("🌍 outside: a1\ninside: value2 value3\noutside: a4").toUtf8());
        editor.undo();
        QCOMPARE(contents(editor), source);
        QVERIFY(!editor.canUndo());
    }

    void regexRangeRetainsOriginalAnchorContext()
    {
        ScintillaNext editor("context");
        editor.setCodePage(SC_CP_UTF8);
        editor.setText("prefix target suffix");
        Finder finder(&editor);
        finder.options().flags = Scintilla::FindOption::RegExp;
        finder.options().text = "^target";
        QCOMPARE(finder.replaceAllInRange("changed", {7, 13}), 0);
        finder.options().text = "(?<=prefix )target(?= suffix)";
        QCOMPARE(finder.replaceAllInRange("changed", {7, 13}), 1);
        QCOMPARE(contents(editor), QByteArray("prefix changed suffix"));
    }

    void regexReplaceManyMatchesPerformance()
    {
        ScintillaNext editor("performance");
        editor.setCodePage(SC_CP_UTF8);
        const QByteArray source = QByteArray("value=1234\n").repeated(100000);
        editor.setText(source.constData());
        editor.emptyUndoBuffer();
        Finder finder(&editor);
        finder.options().text = "value=([0-9]+)";
        finder.options().flags = Scintilla::FindOption::RegExp;
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(finder.replaceAll("number=\\1"), 100000);
        qInfo("Regex replace benchmark: %lld input bytes, 100000 matches, %lld ms", qlonglong(source.size()), timer.elapsed());
        QCOMPARE(contents(editor), QByteArray("number=1234\n").repeated(100000));
        editor.undo();
        QCOMPARE(contents(editor), source);
        QVERIFY(!editor.canUndo());
    }
};

QTEST_MAIN(EditorCoreTest)
#include "EditorCoreTest.moc"
