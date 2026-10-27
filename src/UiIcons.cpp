// SPDX-License-Identifier: GPL-3.0-or-later
#include "UiIcons.h"
#include "ExplorerFileTypeIcon.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QToolBar>
#include <QWidget>
#include <cmath>

namespace {
void document(QPainter &p)
{
    QPainterPath path;
    path.moveTo(3, 1.5); path.lineTo(9, 1.5); path.lineTo(13, 5.5);
    path.lineTo(13, 14.5); path.lineTo(3, 14.5); path.closeSubpath();
    p.drawPath(path);
    p.drawPolyline(QPolygonF{QPointF(9, 1.5), QPointF(9, 5.5), QPointF(13, 5.5)});
}

void folder(QPainter &p)
{
    QPainterPath path;
    path.moveTo(1.5, 4); path.lineTo(6, 4); path.lineTo(8, 6);
    path.lineTo(14.5, 6); path.lineTo(14.5, 13.5); path.lineTo(1.5, 13.5); path.closeSubpath();
    p.drawPath(path);
    p.drawLine(QPointF(1.5, 7.5), QPointF(14.5, 7.5));
}

void magnifier(QPainter &p)
{
    p.drawEllipse(QRectF(2, 1.8, 8.3, 8.3));
    p.drawLine(QPointF(9.1, 9.1), QPointF(14, 14));
}

void save(QPainter &p)
{
    QPainterPath path;
    path.moveTo(2, 1.8); path.lineTo(11.4, 1.8); path.lineTo(14, 4.4);
    path.lineTo(14, 14); path.lineTo(2, 14); path.closeSubpath();
    p.drawPath(path);
    p.drawRect(QRectF(5, 2, 5.8, 4));
    p.drawRect(QRectF(5, 9, 6, 5));
}

void drawGlyph(QPainter &p, UiIcon kind, QIcon::State state, const QPalette &palette, QIcon::Mode mode)
{
    const auto line = [&](double x1, double y1, double x2, double y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
    if (kind == UiIcon::SaveAll || kind == UiIcon::CloseAll) {
        line(1.5, 4, 1.5, 14.5); line(1.5, 14.5, 11, 14.5);
        p.save(); p.translate(2, -1); p.scale(.82, .82);
        drawGlyph(p, kind == UiIcon::SaveAll ? UiIcon::Save : UiIcon::Close, state, palette, mode);
        p.restore();
        return;
    }
    switch (kind) {
    case UiIcon::Dismiss: line(4.5, 4.5, 11.5, 11.5); line(11.5, 4.5, 4.5, 11.5); break;
    case UiIcon::Menu: line(3, 4, 13, 4); line(3, 8, 13, 8); line(3, 12, 13, 12); break;
    case UiIcon::Plus: line(8, 3, 8, 13); line(3, 8, 13, 8); break;
    case UiIcon::File: document(p); break;
    case UiIcon::NewFile:
    case UiIcon::Note:
        document(p); line(8, 7.4, 8, 12.3); line(5.6, 9.8, 10.4, 9.8); break;
    case UiIcon::Folder: folder(p); break;
    case UiIcon::Open:
        folder(p); line(8, 10.6, 8, 2); line(5.8, 4.2, 8, 2); line(10.2, 4.2, 8, 2); break;
    case UiIcon::Save: save(p); break;
    case UiIcon::Close:
        document(p); line(6, 8, 10, 12); line(10, 8, 6, 12); break;
    case UiIcon::Undo:
    case UiIcon::Redo: {
        p.save();
        if (kind == UiIcon::Redo) { p.translate(16, 0); p.scale(-1, 1); }
        QPainterPath path; path.moveTo(3, 5); path.cubicTo(14, 2, 17, 14, 6, 13);
        p.drawPath(path); line(3, 5, 6, 1.8); line(3, 5, 6.8, 7.1); p.restore(); break;
    }
    case UiIcon::Cut:
        p.drawEllipse(QRectF(1.5, 10, 4, 4)); p.drawEllipse(QRectF(10.5, 10, 4, 4));
        line(4.8, 10.7, 12.5, 2); line(11.2, 10.7, 3.5, 2); break;
    case UiIcon::Copy:
        p.drawRect(QRectF(5.4, 5.1, 8, 9.2)); p.drawPolyline(QPolygonF{QPointF(3, 11), QPointF(2, 11), QPointF(2, 1.7), QPointF(10, 1.7), QPointF(10, 3)}); break;
    case UiIcon::Paste:
        p.drawRect(QRectF(3, 3.6, 10, 10.9)); p.drawRoundedRect(QRectF(5.2, 1.6, 5.6, 3.3), .8, .8);
        line(5.4, 8, 10.6, 8); line(5.4, 11, 9.7, 11); break;
    case UiIcon::Trash:
        line(2, 4, 14, 4); p.drawPolyline(QPolygonF{QPointF(3.5, 4), QPointF(4.2, 14), QPointF(11.8, 14), QPointF(12.5, 4)});
        p.drawPolyline(QPolygonF{QPointF(5.5, 3.5), QPointF(5.5, 1.8), QPointF(10.5, 1.8), QPointF(10.5, 3.5)});
        line(6.3, 6.5, 6.3, 11.5); line(9.7, 6.5, 9.7, 11.5); break;
    case UiIcon::Search: magnifier(p); break;
    case UiIcon::SearchFiles:
        p.save(); p.translate(1, 0); p.scale(.75, .75); document(p); p.restore();
        p.drawEllipse(QRectF(7, 7, 5.6, 5.6)); line(12, 12, 14.5, 14.5); break;
    case UiIcon::Replace:
        line(2, 4, 13, 4); line(10.5, 1.5, 13, 4); line(10.5, 6.5, 13, 4);
        line(14, 12, 3, 12); line(5.5, 9.5, 3, 12); line(5.5, 14.5, 3, 12); break;
    case UiIcon::ZoomIn:
    case UiIcon::ZoomOut:
        magnifier(p); line(4, 6, 8.2, 6); if (kind == UiIcon::ZoomIn) line(6.1, 3.9, 6.1, 8.1); break;
    case UiIcon::Indent:
    case UiIcon::Unindent:
        line(2, 2.5, 14, 2.5); line(7, 6, 14, 6); line(7, 10, 14, 10); line(2, 13.5, 14, 13.5);
        if (kind == UiIcon::Indent) { line(2, 5.5, 4.5, 8); line(2, 10.5, 4.5, 8); }
        else { line(4.5, 5.5, 2, 8); line(4.5, 10.5, 2, 8); } break;
    case UiIcon::Wrap: {
        line(2, 3, 14, 3); line(2, 7, 11, 7); line(2, 12, 5, 12);
        QPainterPath path; path.moveTo(11, 7); path.cubicTo(15, 7, 15, 12, 11, 12); path.lineTo(8, 12);
        p.drawPath(path); line(10, 10, 8, 12); line(10, 14, 8, 12); break;
    }
    case UiIcon::Paragraph: {
        QPainterPath path; path.moveTo(8, 8); path.cubicTo(1, 8, 1, 2, 8, 2); path.lineTo(13, 2);
        p.drawPath(path); line(8, 2, 8, 14); line(11, 2, 11, 14); break;
    }
    case UiIcon::IndentGuide:
        p.save(); p.setPen(QPen(p.pen().color(), 1, Qt::DotLine)); line(3, 1.5, 3, 14.5); line(7, 5.5, 7, 14.5); p.restore();
        line(5, 3, 13.5, 3); line(9, 7, 13.5, 7); line(9, 11, 13.5, 11); break;
    case UiIcon::Refresh:
        p.drawArc(QRectF(2.3, 2.3, 11.4, 11.4), 40 * 16, 290 * 16);
        line(12.5, 1.5, 12.5, 5); line(9, 5, 12.5, 5); break;
    case UiIcon::Print:
        p.drawRect(QRectF(4, 1.5, 8, 4)); p.drawRoundedRect(QRectF(1.5, 5.5, 13, 6), 1, 1);
        p.drawRect(QRectF(4, 9, 8, 5.5)); line(11.5, 7.6, 12.1, 7.6); break;
    case UiIcon::Settings: {
        QPolygonF outline;
        constexpr double pi = 3.141592653589793;
        for (int i = 0; i < 32; ++i) {
            const double radius = (i % 4 == 1 || i % 4 == 2) ? 6.5 : 5.2;
            const double angle = i * pi / 16;
            outline << QPointF(8 + radius * std::cos(angle), 8 + radius * std::sin(angle));
        }
        p.drawPolygon(outline); p.drawEllipse(QRectF(5.6, 5.6, 4.8, 4.8)); break;
    }
    case UiIcon::Record: {
        const QColor color = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::ButtonText)
                                                    : palette.color(QPalette::BrightText);
        p.setPen(QPen(color, 1.2)); p.setBrush(color);
        if (state == QIcon::On) p.drawRoundedRect(QRectF(4, 4, 8, 8), 1, 1);
        else p.drawEllipse(QRectF(3.7, 3.7, 8.6, 8.6)); break;
    }
    case UiIcon::Play:
        p.drawPolygon(QPolygonF{QPointF(5, 2.7), QPointF(13, 8), QPointF(5, 13.3)}); break;
    case UiIcon::Repeat:
        p.drawPolygon(QPolygonF{QPointF(2, 3), QPointF(7, 8), QPointF(2, 13)});
        p.drawPolygon(QPolygonF{QPointF(9, 3), QPointF(14, 8), QPointF(9, 13)}); break;
    case UiIcon::MacroSave:
        save(p); p.setBrush(p.pen().color()); p.drawEllipse(QRectF(6.7, 10.5, 2.6, 2.6)); break;
    case UiIcon::MacroEdit:
        line(2, 3, 11, 3); line(2, 6.5, 9, 6.5); line(2, 10, 6, 10);
        p.drawPolygon(QPolygonF{QPointF(6.5, 14), QPointF(7.3, 10.5), QPointF(12.6, 5.2), QPointF(14.8, 7.4), QPointF(9.5, 12.7)}); break;
    case UiIcon::Terminal:
        p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1.3, 1.3);
        line(4, 6, 6.5, 8); line(6.5, 8, 4, 10); line(8, 10, 11.5, 10); break;
    case UiIcon::Info:
        p.drawEllipse(QRectF(1.7, 1.7, 12.6, 12.6)); line(8, 7, 8, 11.3);
        p.setBrush(p.pen().color()); p.drawEllipse(QRectF(7.4, 4.2, 1.2, 1.2)); break;
    case UiIcon::Theme:
        p.drawEllipse(QRectF(1.6, 1.6, 12.8, 12.8));
        p.setBrush(p.pen().color()); p.drawPie(QRectF(1.6, 1.6, 12.8, 12.8), 90 * 16, 180 * 16); break;
    case UiIcon::Keyboard:
        p.drawRoundedRect(QRectF(1.2, 3.5, 13.6, 9), 1, 1);
        for (int x = 4; x <= 12; x += 3) { line(x, 6.5, x + .5, 6.5); line(x, 8.7, x + .5, 8.7); }
        line(5, 10.5, 11, 10.5); break;
    case UiIcon::Vim: line(2.5, 3, 8, 13); line(8, 13, 13.5, 3); break;
    case UiIcon::SplitHorizontal:
    case UiIcon::SplitVertical:
    case UiIcon::Layout:
        p.drawRoundedRect(QRectF(1.5, 2, 13, 12), .8, .8);
        if (kind == UiIcon::SplitHorizontal) line(1.5, 8, 14.5, 8);
        else line(kind == UiIcon::Layout ? 5 : 8, 2, kind == UiIcon::Layout ? 5 : 8, 14); break;
    case UiIcon::Font:
        line(2, 13.5, 7, 2.5); line(7, 2.5, 12, 13.5); line(4, 9, 10, 9); line(11, 5, 15, 5); break;
    case UiIcon::Markdown:
        p.drawRoundedRect(QRectF(1.2, 3, 13.6, 10), 1, 1);
        p.drawPolyline(QPolygonF{QPointF(3.5, 10.5), QPointF(3.5, 5.5), QPointF(6, 8.2), QPointF(8.5, 5.5), QPointF(8.5, 10.5)});
        line(12, 5.5, 12, 10.5); line(10.5, 9, 12, 10.5); line(13.5, 9, 12, 10.5); break;
    default: break;
    }
}

class UiIconEngine final : public QIconEngine
{
public:
    UiIconEngine(UiIcon kind, QWidget *context) : kind(kind), context(context) {}
    QIconEngine *clone() const override { return new UiIconEngine(kind, context); }
    QString key() const override { return QStringLiteral("BitPadUiVector"); }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    { return scaledPixmap(size, mode, state, 1); }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap image(size * scale); image.setDevicePixelRatio(scale); image.fill(Qt::transparent);
        QPainter painter(&image); paint(&painter, QRect(QPoint(), size), mode, state); return image;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const QPalette palette = context ? context->palette() : QApplication::palette();
        const QColor color = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::ButtonText)
            : palette.color(mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText);
        painter->save(); painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x(), rect.y()); painter->scale(rect.width() / 16.0, rect.height() / 16.0);
        painter->setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); painter->setBrush(Qt::NoBrush);
        drawGlyph(*painter, kind, state, palette, mode); painter->restore();
    }
private:
    UiIcon kind;
    QPointer<QWidget> context;
};

class DocumentIconEngine final : public QIconEngine
{
public:
    DocumentIconEngine(QIcon base, bool modified, bool readOnly, QWidget *context)
        : base(base), modified(modified), readOnly(readOnly), context(context) {}
    QIconEngine *clone() const override { return new DocumentIconEngine(base, modified, readOnly, context); }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    { return scaledPixmap(size, mode, state, 1); }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap image(size * scale); image.setDevicePixelRatio(scale); image.fill(Qt::transparent);
        QPainter painter(&image); paint(&painter, QRect(QPoint(), size), mode, state); return image;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        base.paint(painter, rect, Qt::AlignCenter, mode, state);
        if (!modified && !readOnly) return;
        const QPalette palette = context ? context->palette() : QApplication::palette();
        painter->save(); painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x(), rect.y()); painter->scale(rect.width() / 16.0, rect.height() / 16.0);
        const QColor foreground = palette.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::Text);
        painter->setPen(QPen(palette.color(QPalette::Base), 1.8));
        painter->setBrush(modified ? palette.color(QPalette::Link) : foreground);
        if (readOnly) {
            painter->drawRoundedRect(QRectF(8.8, 9.5, 6.3, 5.3), 1, 1);
            painter->setPen(QPen(foreground, 1.2)); painter->setBrush(Qt::NoBrush);
            painter->drawArc(QRectF(10.1, 6.2, 3.6, 6), 0, 180 * 16);
        } else {
            painter->drawEllipse(QRectF(10.2, 10.2, 5.2, 5.2));
        }
        painter->restore();
    }
private:
    QIcon base;
    bool modified;
    bool readOnly;
    QPointer<QWidget> context;
};
}

QIcon uiIcon(UiIcon kind, QWidget *context)
{
    return QIcon(new UiIconEngine(kind, context));
}

QIcon documentTabIcon(const QFileInfo &file, bool modified, bool readOnly, QWidget *context)
{
    return QIcon(new DocumentIconEngine(explorerFileTypeIcon(file), modified, readOnly, context));
}

void installUiIcons(QWidget *root)
{
    if (!root) return;
    static const QHash<QString, UiIcon> actions = {
        {"actionNew", UiIcon::NewFile}, {"actionOpen", UiIcon::Open}, {"actionOpenFolderasWorkspace", UiIcon::Folder},
        {"actionSave", UiIcon::Save}, {"actionSaveAs", UiIcon::Save}, {"actionSaveCopyAs", UiIcon::Save},
        {"actionSaveAll", UiIcon::SaveAll}, {"actionClose", UiIcon::Close}, {"actionCloseAll", UiIcon::CloseAll},
        {"actionUndo", UiIcon::Undo}, {"actionRedo", UiIcon::Redo}, {"actionCut", UiIcon::Cut},
        {"actionCopy", UiIcon::Copy}, {"actionPaste", UiIcon::Paste}, {"actionMoveToTrash", UiIcon::Trash},
        {"actionFind", UiIcon::Search}, {"actionFindInFiles", UiIcon::SearchFiles}, {"actionReplace", UiIcon::Replace},
        {"actionQuickFind", UiIcon::Search}, {"actionZoomIn", UiIcon::ZoomIn}, {"actionZoomOut", UiIcon::ZoomOut},
        {"actionIncreaseIndent", UiIcon::Indent}, {"actionDecreaseIndent", UiIcon::Unindent},
        {"actionWordWrap", UiIcon::Wrap}, {"actionShowAllCharacters", UiIcon::Paragraph},
        {"actionShowIndentGuide", UiIcon::IndentGuide}, {"actionReload", UiIcon::Refresh}, {"actionPrint", UiIcon::Print},
        {"actionPreferences", UiIcon::Settings}, {"actionMacroRecording", UiIcon::Record},
        {"actionPlayback", UiIcon::Play}, {"actionRunMacroMultipleTimes", UiIcon::Repeat},
        {"actionSaveCurrentRecordedMacro", UiIcon::MacroSave}, {"actionEditMacros", UiIcon::MacroEdit},
        {"actionShowInExplorer", UiIcon::Folder}, {"actionOpenTerminalHere", UiIcon::Terminal},
        {"actionAboutNotepadNext", UiIcon::Info}, {"actionDebugInfo", UiIcon::Info},
        {"actionKeyboardShortcuts", UiIcon::Keyboard}, {"actionCommandPalette", UiIcon::Terminal},
        {"actionVimMode", UiIcon::Vim}, {"actionNewNote", UiIcon::Note}, {"actionTakeNotesSettings", UiIcon::Settings},
        {"actionSplitHorizontal", UiIcon::SplitHorizontal}, {"actionSplitVertical", UiIcon::SplitVertical},
        {"actionMarkdownPreview", UiIcon::Markdown}
    };
    for (QAction *action : root->findChildren<QAction *>()) {
        const auto found = actions.constFind(action->objectName());
        if (found != actions.cend()) action->setIcon(uiIcon(*found, root));
    }
    for (QToolBar *toolbar : root->findChildren<QToolBar *>()) toolbar->setIconSize(QSize(16, 16));
}

void refreshUiIcons(QWidget *root)
{
    installUiIcons(root);
    if (root) root->update();
}
