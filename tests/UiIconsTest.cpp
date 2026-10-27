// SPDX-License-Identifier: GPL-3.0-or-later
#include "UiIcons.h"
#include "ExplorerFileTypeIcon.h"

#include <QAction>
#include <QFileInfo>
#include <QImage>
#include <QPalette>
#include <QWidget>
#include <QtTest>

class UiIconsTest : public QObject
{
    Q_OBJECT
private slots:
    void everyGlyphRendersAtHighDpi();
    void paletteAndDisabledState();
    void documentStatesAreDistinct();
    void installingPreservesActionBehavior();
};

void UiIconsTest::everyGlyphRendersAtHighDpi()
{
    for (int kind = int(UiIcon::File); kind <= int(UiIcon::Plus); ++kind) {
        const QPixmap rendered = uiIcon(UiIcon(kind)).pixmap(QSize(16, 16), 2.0);
        QVERIFY2(!rendered.isNull(), qPrintable(QString::number(kind)));
        QCOMPARE(rendered.size(), QSize(32, 32));
        QCOMPARE(rendered.devicePixelRatio(), 2.0);
        const QImage pixels = rendered.toImage();
        bool drawn = false;
        for (int y = 0; y < pixels.height() && !drawn; ++y)
            for (int x = 0; x < pixels.width(); ++x)
                if (pixels.pixelColor(x, y).alpha() > 0) { drawn = true; break; }
        QVERIFY2(drawn, qPrintable(QString("Empty glyph %1").arg(kind)));
    }
}

void UiIconsTest::paletteAndDisabledState()
{
    QWidget context;
    QPalette palette = context.palette();
    palette.setColor(QPalette::ButtonText, QColor("#15191e"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#777777"));
    context.setPalette(palette);
    const QIcon icon = uiIcon(UiIcon::Save, &context);
    const QImage light = icon.pixmap(QSize(24, 24)).toImage();
    const QImage disabled = icon.pixmap(QSize(24, 24), QIcon::Disabled).toImage();
    QVERIFY(light != disabled);
    palette.setColor(QPalette::ButtonText, QColor("#ebeff5"));
    context.setPalette(palette);
    const QImage dark = icon.pixmap(QSize(24, 24)).toImage();
    QVERIFY(light != dark);
}

void UiIconsTest::documentStatesAreDistinct()
{
    const QFileInfo file("example.py");
    const auto clean = documentTabIcon(file, false, false).pixmap(QSize(24, 24)).toImage();
    const auto dirty = documentTabIcon(file, true, false).pixmap(QSize(24, 24)).toImage();
    const auto locked = documentTabIcon(file, false, true).pixmap(QSize(24, 24)).toImage();
    QVERIFY(clean != dirty);
    QVERIFY(clean != locked);
    QVERIFY(dirty != locked);
    QVERIFY(explorerFileTypeIcon(QFileInfo("script.py")).pixmap(QSize(24, 24)).toImage()
            != explorerFileTypeIcon(QFileInfo("README.md")).pixmap(QSize(24, 24)).toImage());
}

void UiIconsTest::installingPreservesActionBehavior()
{
    QWidget root;
    QAction action("Save this file", &root);
    action.setObjectName("actionSave");
    action.setShortcut(QKeySequence::Save);
    action.setEnabled(false);
    installUiIcons(&root);
    QVERIFY(!action.icon().isNull());
    QCOMPARE(action.text(), QString("Save this file"));
    QCOMPARE(action.shortcut(), QKeySequence(QKeySequence::Save));
    QVERIFY(!action.isEnabled());
    refreshUiIcons(&root);
    QVERIFY(!action.isEnabled());
}

QTEST_MAIN(UiIconsTest)
#include "UiIconsTest.moc"
