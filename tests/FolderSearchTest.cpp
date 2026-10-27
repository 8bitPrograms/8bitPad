#include "FolderSearch.h"

#include <QDir>
#include <QFile>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QtTest>

class FolderSearchTest : public QObject
{
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &text)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
    }
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    static FolderSearch::Cancellation token() { return std::make_shared<std::atomic_bool>(false); }

private slots:
    void recursiveMasksAndBinarySkipping()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("nested"));
        QVERIFY(write(directory.filePath("first.txt"), "find\r\nfind"));
        QVERIFY(write(directory.filePath("ignore.cpp"), "find"));
        QVERIFY(write(directory.filePath("nested/second.txt"), "find"));
        QVERIFY(write(directory.filePath("binary.txt"), QByteArray("find\0binary", 11)));
        FolderSearch::Options options;
        options.directory = directory.path();
        options.pattern = "find";
        options.masks = {"*.txt"};
        const auto result = FolderSearch::scan(options, token());
        QCOMPARE(result.files.size(), 2);
        QCOMPARE(result.matches, 3);
        QCOMPARE(result.skipped, 1);
        options.recursive = false;
        QCOMPARE(FolderSearch::scan(options, token()).matches, 2);
    }

    void replacementPreservesBomEolAndBackup_data()
    {
        QTest::addColumn<QByteArray>("bom");
        QTest::addColumn<int>("encoding");
        QTest::newRow("utf8") << QByteArray() << int(QStringConverter::Utf8);
        QTest::newRow("utf8-bom") << QByteArray::fromHex("efbbbf") << int(QStringConverter::Utf8);
        QTest::newRow("utf16le") << QByteArray::fromHex("fffe") << int(QStringConverter::Utf16LE);
        QTest::newRow("utf16be") << QByteArray::fromHex("feff") << int(QStringConverter::Utf16BE);
    }

    void replacementPreservesBomEolAndBackup()
    {
        QFETCH(QByteArray, bom);
        QFETCH(int, encoding);
        QTemporaryDir directory;
        const QString path = directory.filePath("text.txt");
        QStringEncoder encoder(static_cast<QStringConverter::Encoding>(encoding));
        const QByteArray original = bom + QByteArray(encoder(QString::fromUtf8("Türkçe find\r\n🌍 find\r\n")));
        QVERIFY(write(path, original));
        FolderSearch::Options options;
        options.directory = directory.path();
        options.pattern = "find";
        auto preview = FolderSearch::scan(options, token());
        QCOMPARE(preview.matches, 2);
        const auto result = FolderSearch::replace(preview, "done", {}, token());
        QCOMPARE(result.files, 1);
        QCOMPARE(result.matches, 2);
        QCOMPARE(result.backups.size(), 1);
        QCOMPARE(read(result.backups.first()), original);
        QStringEncoder expectedEncoder(static_cast<QStringConverter::Encoding>(encoding));
        QCOMPARE(read(path), bom + QByteArray(expectedEncoder(QString::fromUtf8("Türkçe done\r\n🌍 done\r\n"))));
    }

    void refusesChangedAndOpenFiles()
    {
        QTemporaryDir directory;
        const QString changed = directory.filePath("changed.txt");
        const QString opened = directory.filePath("open.txt");
        QVERIFY(write(changed, "old"));
        QVERIFY(write(opened, "old"));
        FolderSearch::Options options;
        options.directory = directory.path();
        options.pattern = "old";
        const auto preview = FolderSearch::scan(options, token());
        QVERIFY(write(changed, "new from external editor"));
        const auto result = FolderSearch::replace(preview, "replacement", {opened}, token());
        QCOMPARE(result.files, 0);
        QCOMPARE(result.skipped, 2);
        QCOMPARE(read(changed), QByteArray("new from external editor"));
        QCOMPARE(read(opened), QByteArray("old"));
        QVERIFY(result.backups.isEmpty());
    }

    void regexCapturesAndZeroWidth()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("text.txt");
        QVERIFY(write(path, "a1 a2"));
        FolderSearch::Options options;
        options.directory = directory.path();
        options.pattern = "(?=(a)([0-9]))";
        options.regex = true;
        const auto preview = FolderSearch::scan(options, token());
        QCOMPARE(preview.matches, 2);
        const auto result = FolderSearch::replace(preview, "\\2\\1_", {}, token());
        QCOMPARE(result.matches, 2);
        QCOMPARE(read(path), QByteArray("1a_a1 2a_a2"));
    }

    void boundedAndCancelledPreviewsCannotReplace()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("text.txt");
        QVERIFY(write(path, "aaaa"));
        FolderSearch::Options options;
        options.directory = directory.path();
        options.pattern = "a";
        options.maxMatches = 2;
        const auto truncated = FolderSearch::scan(options, token());
        QVERIFY(truncated.truncated);
        QCOMPARE(truncated.matches, 2);
        QVERIFY(!FolderSearch::replace(truncated, "b", {}, token()).errors.isEmpty());
        QCOMPARE(read(path), QByteArray("aaaa"));
        auto cancel = token();
        cancel->store(true);
        QVERIFY(FolderSearch::scan(options, cancel).cancelled);
    }

    void wholeWordUnicodeAndCoordinates()
    {
        QTemporaryDir directory;
        QVERIFY(write(directory.filePath("words.txt"), QString::fromUtf8("écho\r\nfind finder\rfind").toUtf8()));
        FolderSearch::Options options;
        options.directory = directory.path();
        options.pattern = "find";
        options.wholeWord = true;
        const auto result = FolderSearch::scan(options, token());
        QCOMPARE(result.matches, 2);
        QCOMPARE(result.files.first().matches.at(0).line, 2);
        QCOMPARE(result.files.first().matches.at(0).column, 1);
        QCOMPARE(result.files.first().matches.at(1).line, 3);
        QCOMPARE(result.files.first().matches.at(1).column, 1);
    }

    void invalidEncodingAndOversizeAreSkipped()
    {
        QTemporaryDir directory;
        QVERIFY(write(directory.filePath("invalid.txt"), QByteArray::fromHex("ffff66696e64")));
        QVERIFY(write(directory.filePath("odd-utf16.txt"), QByteArray::fromHex("fffe6600ff")));
        QVERIFY(write(directory.filePath("high-surrogate.txt"), QByteArray::fromHex("fffe00d8660069006e006400")));
        QVERIFY(write(directory.filePath("low-surrogate.txt"), QByteArray::fromHex("fffe660069006e00640000dc")));
        QVERIFY(write(directory.filePath("huge.txt"), QByteArray(100, 'a')));
        FolderSearch::Options options;
        options.directory = directory.path();
        options.pattern = "find";
        options.maxFileBytes = 50;
        const auto result = FolderSearch::scan(options, token());
        QCOMPARE(result.matches, 0);
        QCOMPARE(result.skipped, 5);
    }
};

QTEST_GUILESS_MAIN(FolderSearchTest)
#include "FolderSearchTest.moc"
