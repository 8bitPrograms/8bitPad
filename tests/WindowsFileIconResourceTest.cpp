// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the Windows icon loader against the actual application payload.
// This test never registers file types, touches the registry, or starts Pad.
#include "FileTypeIcons.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QtEndian>
#include <QtTest>
#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {
struct NativeIcon {
    HICON handle = nullptr;
    ~NativeIcon() { if (handle) DestroyIcon(handle); }
    NativeIcon() = default;
    NativeIcon(const NativeIcon &) = delete;
    NativeIcon &operator=(const NativeIcon &) = delete;
};

QByteArray resource(HMODULE module, int id, LPCWSTR type)
{
    const HRSRC record = FindResourceW(module, MAKEINTRESOURCEW(id), type);
    if (!record) return {};
    const DWORD size = SizeofResource(module, record);
    const void *bytes = LockResource(LoadResource(module, record));
    return bytes && size ? QByteArray(static_cast<const char *>(bytes), size) : QByteArray{};
}

quint16 word(const QByteArray &bytes, qsizetype offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint32 dword(const QByteArray &bytes, qsizetype offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

HICON iconAtSize(HMODULE module, int id, int size)
{
    QByteArray group = resource(module, id, RT_GROUP_ICON);
    if (group.size() < 6 || group.size() < 6 + 14 * word(group, 4)) return nullptr;
    const int imageId = LookupIconIdFromDirectoryEx(reinterpret_cast<PBYTE>(group.data()),
        TRUE, size, size, LR_DEFAULTCOLOR);
    QByteArray pixels = resource(module, imageId, RT_ICON);
    if (pixels.isEmpty()) return nullptr;
    return CreateIconFromResourceEx(reinterpret_cast<PBYTE>(pixels.data()), DWORD(pixels.size()),
        TRUE, 0x00030000, size, size, LR_DEFAULTCOLOR);
}

QImage renderIcon(HICON icon, int size, const QColor &background)
{
    if (!icon) return {};
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return {};
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(dc);
        return {};
    }
    const HGDIOBJ previous = SelectObject(dc, bitmap);
    std::fill_n(static_cast<QRgb *>(bits), size * size, background.rgba());
    const BOOL drawn = DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
    // RGB32 ignores the destination alpha that legacy GDI drawing may clear.
    const QImage image = drawn
        ? QImage(static_cast<uchar *>(bits), size, size, QImage::Format_RGB32).copy()
        : QImage{};
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return image;
}

QByteArray imageHash(const QImage &image)
{
    return QCryptographicHash::hash(QByteArrayView(
        reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()), QCryptographicHash::Sha256);
}

struct ResourceNames { QSet<int> ids; bool named = false; };
BOOL CALLBACK collectGroups(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR context)
{
    auto *names = reinterpret_cast<ResourceNames *>(context);
    if (IS_INTRESOURCE(name)) names->ids.insert(int(reinterpret_cast<ULONG_PTR>(name)));
    else names->named = true;
    return TRUE;
}
}

class WindowsFileIconResourceTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void stableGroupIdsAndNativeSizes();
    void everyTypeRendersDistinctlyOnLightAndDark();
    void applicationIconIsUnchangedAndRemainsFirst();
    void cleanupTestCase();
private:
    QString executable;
    HMODULE module = nullptr;
};

void WindowsFileIconResourceTest::initTestCase()
{
    executable = QDir::toNativeSeparators(QStringLiteral(BIT8PAD_TEST_EXECUTABLE));
    QVERIFY2(QFile::exists(executable), qPrintable(executable));
    module = LoadLibraryExW(executable.toStdWString().c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    QVERIFY2(module, qPrintable(QString("Cannot read PE resources: %1").arg(GetLastError())));
}

void WindowsFileIconResourceTest::stableGroupIdsAndNativeSizes()
{
    ResourceNames names;
    QVERIFY(EnumResourceNamesW(module, RT_GROUP_ICON, collectGroups, reinterpret_cast<LONG_PTR>(&names)));
    QVERIFY(!names.named);
    QSet<int> expected{FileTypeIcons::ApplicationResourceId};
    QVERIFY(!FileTypeIcons::catalog().isEmpty());
    for (const auto &type : FileTypeIcons::catalog()) {
        QVERIFY2(type.resourceId >= 201 && type.resourceId <= 65535, qPrintable(type.id));
        QVERIFY2(!expected.contains(type.resourceId), qPrintable(type.id));
        expected.insert(type.resourceId);
        const QByteArray group = resource(module, type.resourceId, RT_GROUP_ICON);
        QVERIFY2(group.size() >= 6, qPrintable(type.id));
        QCOMPARE(word(group, 0), quint16(0));
        QCOMPARE(word(group, 2), quint16(1));
        QVERIFY(group.size() >= 6 + 14 * word(group, 4));
        QSet<int> sizes;
        for (int i = 0; i < word(group, 4); ++i) {
            const qsizetype offset = 6 + 14 * i;
            const int width = uchar(group.at(offset)) ? uchar(group.at(offset)) : 256;
            const int height = uchar(group.at(offset + 1)) ? uchar(group.at(offset + 1)) : 256;
            QCOMPARE(width, height);
            sizes.insert(width);
            const QByteArray pixels = resource(module, word(group, offset + 12), RT_ICON);
            QVERIFY2(!pixels.isEmpty(), qPrintable(type.id));
            QCOMPARE(pixels.size(), qsizetype(dword(group, offset + 8)));
        }
        for (int size : {16, 32, 48, 256})
            QVERIFY2(sizes.contains(size), qPrintable(QString("%1 lacks %2px image").arg(type.id).arg(size)));
    }
    QCOMPARE(names.ids, expected);
}

void WindowsFileIconResourceTest::everyTypeRendersDistinctlyOnLightAndDark()
{
    for (int size : {16, 32, 48, 256}) {
        for (const QColor background : {QColor("#f4f4f4"), QColor("#10141d")}) {
            QSet<QByteArray> renderedTypes;
            for (const auto &type : FileTypeIcons::catalog()) {
                const QString description = QString("%1 / %2px / %3").arg(type.id).arg(size).arg(background.name());
                NativeIcon icon;
                icon.handle = iconAtSize(module, type.resourceId, size);
                QVERIFY2(icon.handle, qPrintable(description));
                const QImage image = renderIcon(icon.handle, size, background);
                QCOMPARE(image.size(), QSize(size, size));
                int painted = 0, saturated = 0;
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        const QColor pixel = image.pixelColor(x, y);
                        if (pixel.rgb() != background.rgb()) ++painted;
                        if (pixel.saturation() > 90 && pixel.value() > 90) ++saturated;
                    }
                }
                QVERIFY2(painted > size * size / 8, qPrintable("Blank or barely visible icon: " + description));
                QVERIFY2(saturated > size * size / 20, qPrintable("Missing branded color: " + description));
                // Transparent corners must compose correctly on either Explorer theme.
                QCOMPARE(image.pixelColor(0, 0).rgb(), background.rgb());
                const QByteArray hash = imageHash(image);
                QVERIFY2(!renderedTypes.contains(hash), qPrintable("Duplicate file-type icon: " + description));
                renderedTypes.insert(hash);
            }
        }
    }
}

void WindowsFileIconResourceTest::applicationIconIsUnchangedAndRemainsFirst()
{
    QFile source(QStringLiteral(BIT8PAD_SOURCE_DIR "/icon/8bitPad.ico"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray ico = source.readAll();
    QVERIFY(ico.size() >= 6);
    const quint16 count = word(ico, 4);
    QVERIFY(ico.size() >= 6 + count * 16);
    const QByteArray group = resource(module, FileTypeIcons::ApplicationResourceId, RT_GROUP_ICON);
    QVERIFY(group.size() >= 6);
    QCOMPARE(word(group, 4), count);
    QVERIFY(group.size() >= 6 + count * 14);
    QSet<QByteArray> sourceImages, embeddedImages;
    for (int i = 0; i < count; ++i) {
        const quint32 bytes = dword(ico, 6 + i * 16 + 8);
        const quint32 offset = dword(ico, 6 + i * 16 + 12);
        QVERIFY(qint64(offset) + bytes <= ico.size());
        sourceImages.insert(QCryptographicHash::hash(ico.mid(offset, bytes), QCryptographicHash::Sha256));
        const QByteArray pixels = resource(module, word(group, 6 + i * 14 + 12), RT_ICON);
        QVERIFY(!pixels.isEmpty());
        embeddedImages.insert(QCryptographicHash::hash(pixels, QCryptographicHash::Sha256));
    }
    QCOMPARE(embeddedImages, sourceImages);
    NativeIcon firstLarge, firstSmall, appLarge, appSmall;
    const std::wstring path = executable.toStdWString();
    const UINT firstCount = ExtractIconExW(path.c_str(), 0, &firstLarge.handle, &firstSmall.handle, 1);
    // ExtractIconEx treats -1 as a count query even when icon handles are given.
    // Read resource ID 1 directly, then verify Explorer's first-index route matches it.
    appLarge.handle = iconAtSize(module, FileTypeIcons::ApplicationResourceId, 32);
    appSmall.handle = iconAtSize(module, FileTypeIcons::ApplicationResourceId, 16);
    // Windows can count both returned handles (large + small), not just groups.
    QVERIFY(firstCount > 0 && firstCount <= 2);
    QVERIFY(firstLarge.handle && firstSmall.handle && appLarge.handle && appSmall.handle);
    QCOMPARE(renderIcon(firstLarge.handle, 32, Qt::white), renderIcon(appLarge.handle, 32, Qt::white));
    QCOMPARE(renderIcon(firstSmall.handle, 16, Qt::white), renderIcon(appSmall.handle, 16, Qt::white));
}

void WindowsFileIconResourceTest::cleanupTestCase()
{
    if (module) FreeLibrary(module);
    module = nullptr;
}

QTEST_GUILESS_MAIN(WindowsFileIconResourceTest)
#include "WindowsFileIconResourceTest.moc"
