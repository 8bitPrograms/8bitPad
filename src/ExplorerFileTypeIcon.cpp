// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExplorerFileTypeIcon.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace {
struct FileStyle { QString label; QColor accent; };

FileStyle style(const QFileInfo &file)
{
    static const QHash<QString, FileStyle> styles = {
        {"py", {"Py", QColor("#3778a8")}}, {"pyw", {"Py", QColor("#3778a8")}},
        {"c", {"C", QColor("#5074be")}}, {"h", {"H", QColor("#7965b5")}},
        {"cpp", {"C+", QColor("#5074be")}}, {"cc", {"C+", QColor("#5074be")}},
        {"cxx", {"C+", QColor("#5074be")}}, {"hpp", {"H+", QColor("#7965b5")}},
        {"cs", {"C#", QColor("#8063b1")}}, {"rs", {"Rs", QColor("#b0663b")}},
        {"go", {"Go", QColor("#1685a0")}}, {"rb", {"Rb", QColor("#b3404a")}},
        {"sh", {"$", QColor("#47824d")}}, {"bash", {"$", QColor("#47824d")}},
        {"zsh", {"$", QColor("#47824d")}}, {"ps1", {">_", QColor("#467ec4")}},
        {"psm1", {">_", QColor("#467ec4")}}, {"bat", {">_", QColor("#65824d")}},
        {"cmd", {">_", QColor("#65824d")}}, {"js", {"JS", QColor("#a17b16")}},
        {"jsx", {"JS", QColor("#1685a0")}}, {"ts", {"TS", QColor("#467ec4")}},
        {"tsx", {"TS", QColor("#1685a0")}}, {"java", {"J", QColor("#b3603e")}},
        {"kt", {"Kt", QColor("#9272c0")}}, {"lua", {"Lu", QColor("#6578c0")}},
        {"hs", {"Hs", QColor("#8e65a8")}}, {"html", {"<>", QColor("#b76340")}},
        {"htm", {"<>", QColor("#b76340")}}, {"xml", {"<>", QColor("#b76340")}},
        {"svg", {"<>", QColor("#b18a35")}}, {"css", {"#", QColor("#637ac0")}},
        {"scss", {"#", QColor("#b36190")}}, {"json", {"{}", QColor("#a17b16")}},
        {"jsonc", {"{}", QColor("#a17b16")}}, {"yml", {"Y", QColor("#b26482")}},
        {"yaml", {"Y", QColor("#b26482")}}, {"toml", {"T", QColor("#986b58")}},
        {"ini", {"=", QColor("#778695")}}, {"cfg", {"=", QColor("#778695")}},
        {"md", {"M", QColor("#5084b0")}}, {"markdown", {"M", QColor("#5084b0")}},
        {"mdown", {"M", QColor("#5084b0")}}, {"sql", {"DB", QColor("#b38a34")}},
        {"png", {"▧", QColor("#8a6eb1")}}, {"jpg", {"▧", QColor("#8a6eb1")}},
        {"jpeg", {"▧", QColor("#8a6eb1")}}, {"gif", {"▧", QColor("#8a6eb1")}},
        {"webp", {"▧", QColor("#8a6eb1")}}, {"zip", {"Z", QColor("#a78a54")}},
        {"gz", {"Z", QColor("#a78a54")}}, {"7z", {"Z", QColor("#a78a54")}},
        {"pdf", {"P", QColor("#b35e64")}}, {"cmake", {"△", QColor("#538d8b")}}
    };
    const QString name = file.fileName().toLower();
    if (name == "dockerfile" || name.startsWith("dockerfile."))
        return {"D", QColor("#3b8bb5")};
    if (name == "jenkinsfile" || name.startsWith("jenkinsfile."))
        return {"J", QColor("#b06757")};
    if (name == "cmakelists.txt" || name == "makefile")
        return {"△", QColor("#538d8b")};
    if (name == ".gitignore" || name == ".gitattributes")
        return {"G", QColor("#b36c48")};
    return styles.value(file.suffix().toLower(), {QString(), QColor()});
}

class FileIconEngine final : public QIconEngine
{
public:
    FileIconEngine(bool directory, FileStyle fileStyle) : directory(directory), fileStyle(fileStyle) {}
    QIconEngine *clone() const override { return new FileIconEngine(directory, fileStyle); }
    QString key() const override { return QStringLiteral("BitPadExplorerVector"); }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap image(size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return image;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const auto palette = QGuiApplication::palette();
        const bool dark = palette.color(QPalette::Base).lightness() < 128;
        QColor color = directory ? QColor(dark ? "#d6b66c" : "#907129") : fileStyle.accent;
        if (!color.isValid())
            color = palette.color(QPalette::Text);
        else if (!directory && dark)
            color = color.lighter(145);
        if (mode == QIcon::Disabled)
            color.setAlpha(100);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x(), rect.y());
        painter->scale(rect.width() / 16.0, rect.height() / 16.0);
        painter->setPen(QPen(color, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QColor fill = color;
        fill.setAlpha(dark ? 24 : 14);
        painter->setBrush(fill);
        QPainterPath outline;
        if (directory) {
            outline.moveTo(1.6, 4.2); outline.lineTo(6.4, 4.2); outline.lineTo(8.0, 5.7);
            outline.lineTo(14.4, 5.7); outline.lineTo(14.4, 13.1); outline.lineTo(1.6, 13.1);
            outline.closeSubpath();
            painter->drawPath(outline);
            painter->drawLine(QPointF(2.0, 7.1), QPointF(14.0, 7.1));
        } else {
            outline.moveTo(3.0, 1.2); outline.lineTo(9.4, 1.2); outline.lineTo(13.1, 4.9);
            outline.lineTo(13.1, 14.7); outline.lineTo(3.0, 14.7); outline.closeSubpath();
            painter->drawPath(outline);
            painter->drawPolyline(QPolygonF{QPointF(9.4, 1.7), QPointF(9.4, 4.9), QPointF(12.7, 4.9)});
            if (fileStyle.label.isEmpty()) {
                painter->drawLine(QPointF(5.1, 8.0), QPointF(10.7, 8.0));
                painter->drawLine(QPointF(5.1, 10.5), QPointF(9.3, 10.5));
            } else {
                QFont font = QGuiApplication::font();
                font.setPixelSize(fileStyle.label.size() > 1 ? 7 : 9);
                font.setBold(true);
                painter->setFont(font);
                painter->drawText(QRectF(3.0, 5.3, 10.1, 8.4), Qt::AlignCenter, fileStyle.label);
            }
        }
        painter->restore();
    }
private:
    bool directory;
    FileStyle fileStyle;
};
}

QIcon explorerFileTypeIcon(const QFileInfo &file)
{
    return QIcon(new FileIconEngine(file.isDir(), style(file)));
}
