/* Copyright 2026 8bitPad contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "NavigationPanels.h"
#include "dialogs/MainWindow.h"
#include "ScintillaNext.h"

#include <QDockWidget>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

DocumentMapWidget::DocumentMapWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DocumentDensityMap"));
    // Give the overview usable height when QMainWindow first splits the right
    // dock area with the function tree. A bare QWidget has no useful size hint
    // and otherwise gets compressed to a few pixels beside an expanding tree.
    setMinimumSize(110, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(false);
    setAccessibleName(tr("Document map"));
    setAccessibleDescription(tr("Document overview. Click or drag to navigate; arrow keys move by line."));
    setToolTip(tr("Click or drag to navigate. The highlighted band is the visible part of the document."));
}

void DocumentMapWidget::setOverview(const DocumentOverview &value)
{
    overview = value;
    update();
}

void DocumentMapWidget::setVisibleRange(int first, int last)
{
    firstVisible = first;
    lastVisible = last;
    update();
}

int DocumentMapWidget::lineAtY(qreal y) const
{
    return std::clamp(int(y * overview.lineCount / std::max(1, height())), 0, std::max(0, overview.lineCount - 1));
}

void DocumentMapWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    if (overview.rows.isEmpty()) return;
    const qreal rowHeight = qreal(height()) / overview.rows.size();
    const qreal columnWidth = qreal(width() - 12) / 120;
    QColor ink = palette().text().color();
    ink.setAlpha(150);
    for (qsizetype row = 0; row < overview.rows.size(); ++row) {
        const OverviewSpan &span = overview.rows.at(row);
        if (span.width == 0) continue;
        painter.fillRect(QRectF(6 + span.indentation * columnWidth, row * rowHeight,
                               std::max(qreal(1), span.width * columnWidth), std::max(qreal(1), std::min(qreal(2), rowHeight))), ink);
    }
    const qreal top = qreal(firstVisible) * height() / overview.lineCount;
    const qreal bottom = qreal(lastVisible + 1) * height() / overview.lineCount;
    QColor highlight = palette().highlight().color();
    highlight.setAlpha(45);
    painter.fillRect(QRectF(0, top, width(), std::max(qreal(4), bottom - top)), highlight);
    painter.setPen(palette().highlight().color());
    painter.drawRect(QRectF(0.5, top, width() - 1, std::max(qreal(4), bottom - top)));
}

void DocumentMapWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) emit navigateToLine(lineAtY(event->position().y()));
    event->accept();
}

void DocumentMapWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) emit navigateToLine(lineAtY(event->position().y()));
    event->accept();
}

void DocumentMapWidget::keyPressEvent(QKeyEvent *event)
{
    int target = firstVisible;
    switch (event->key()) {
    case Qt::Key_Up: --target; break;
    case Qt::Key_Down: ++target; break;
    case Qt::Key_PageUp: target -= std::max(1, lastVisible - firstVisible); break;
    case Qt::Key_PageDown: target += std::max(1, lastVisible - firstVisible); break;
    case Qt::Key_Home: target = 0; break;
    case Qt::Key_End: target = overview.lineCount - 1; break;
    default: event->accept(); return;
    }
    emit navigateToLine(std::clamp(target, 0, std::max(0, overview.lineCount - 1)));
    event->accept();
}

NavigationPanels::NavigationPanels(MainWindow *value, NotepadNextApplication *app)
    : QObject(value), window(value)
{
    Q_UNUSED(app);
    setObjectName(QStringLiteral("NavigationPanels"));
    functionDock = new QDockWidget(tr("Function List"), value);
    functionDock->setObjectName(QStringLiteral("BitPadFunctionList"));
    auto *functionContent = new QWidget(functionDock);
    auto *layout = new QVBoxLayout(functionContent);
    layout->setContentsMargins(6, 6, 6, 6);
    filter = new QLineEdit(functionContent);
    filter->setPlaceholderText(tr("Filter functions and types…"));
    filter->setAccessibleName(tr("Filter functions and types"));
    layout->addWidget(filter);
    functionList = new QTreeWidget(functionContent);
    functionList->setColumnCount(2);
    functionList->setHeaderLabels({tr("Symbol"), tr("Line")});
    functionList->setRootIsDecorated(false);
    functionList->setUniformRowHeights(true);
    functionList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    functionList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    functionList->setToolTip(tr("Heuristic outline: functions, methods and types. Double-click or press Enter to navigate."));
    layout->addWidget(functionList);
    functionStatus = new QLabel(functionContent);
    functionStatus->setWordWrap(true);
    layout->addWidget(functionStatus);
    functionDock->setWidget(functionContent);
    value->addDockWidget(Qt::RightDockWidgetArea, functionDock);

    mapDock = new QDockWidget(tr("Document Map"), value);
    mapDock->setObjectName(QStringLiteral("BitPadDocumentMap"));
    auto *mapContent = new QWidget(mapDock);
    auto *mapLayout = new QVBoxLayout(mapContent);
    mapLayout->setContentsMargins(0, 0, 0, 0);
    map = new DocumentMapWidget(mapContent);
    mapLayout->addWidget(map, 1);
    mapStatus = new QLabel(mapContent);
    mapStatus->setWordWrap(true);
    mapStatus->setMargin(6);
    mapLayout->addWidget(mapStatus);
    mapDock->setWidget(mapContent);
    value->addDockWidget(Qt::RightDockWidgetArea, mapDock);

    QMenu *view = value->findChild<QMenu *>(QStringLiteral("menuView"));
    if (!view) view = value->menuBar()->addMenu(tr("View"));
    view->addAction(functionDock->toggleViewAction());
    view->addAction(mapDock->toggleViewAction());
    functionDock->hide();
    mapDock->hide();

    debounce.setInterval(400);
    debounce.setSingleShot(true);
    connect(&debounce, &QTimer::timeout, this, &NavigationPanels::refresh);
    connect(value, &MainWindow::editorActivated, this, &NavigationPanels::attachEditor);
    connect(functionDock, &QDockWidget::visibilityChanged, this, [this](bool visible) { if (visible) scheduleRefresh(); });
    connect(mapDock, &QDockWidget::visibilityChanged, this, [this](bool visible) { if (visible) scheduleRefresh(); });
    connect(filter, &QLineEdit::textChanged, this, &NavigationPanels::populateFunctions);
    connect(functionList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (item) navigateTo(item->data(0, Qt::UserRole).toInt());
    });
    connect(map, &DocumentMapWidget::navigateToLine, this, &NavigationPanels::navigateTo);
    attachEditor(value->currentEditor());
}

NavigationPanels::~NavigationPanels()
{
    debounce.stop();
    if (activeWorker) {
        // The worker owns only a snapshot. Stop and join it before Qt's global
        // facilities are torn down; never leave a live thread behind at exit.
        disconnect(activeWorker, nullptr, this, nullptr);
        activeWorker->requestInterruption();
        activeWorker->wait();
        delete activeWorker.data();
    }
}

void NavigationPanels::attachEditor(ScintillaNext *value)
{
    if (editor) disconnect(editor, nullptr, this, nullptr);
    editor = value;
    symbols.clear();
    functionList->clear();
    map->setOverview({});
    if (editor) {
        connect(editor, &ScintillaNext::modified, this, [this](Scintilla::ModificationFlags flags) {
            if (static_cast<int>(flags) & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) scheduleRefresh();
        });
        connect(editor, &ScintillaNext::lexerChanged, this, &NavigationPanels::scheduleRefresh);
        connect(editor, &ScintillaNext::renamed, this, &NavigationPanels::scheduleRefresh);
        connect(editor, &ScintillaNext::updateUi, this, [this] { updateVisibleRange(); });
        connect(editor, &QObject::destroyed, this, [this] { attachEditor(nullptr); });
    }
    scheduleRefresh();
}

void NavigationPanels::scheduleRefresh()
{
    ++generation;
    refreshPending = true;
    if (functionDock->isVisible() || mapDock->isVisible()) debounce.start();
}

void NavigationPanels::refresh()
{
    if (!functionDock->isVisible() && !mapDock->isVisible()) return;
    if (running) return;
    refreshPending = false;
    if (!editor) {
        functionStatus->setText(tr("No document"));
        mapStatus->setText(tr("No document"));
        return;
    }
    if (editor->length() > FunctionExtractor::MaximumDocumentBytes) {
        symbols.clear(); functionList->clear(); map->setOverview({});
        const QString message = tr("Overview paused for files larger than 20 MiB.");
        functionStatus->setText(message); mapStatus->setText(message);
        return;
    }
    // Take one bounded snapshot on the GUI thread. Parsing and building the map
    // run on a worker which never touches editor or window objects.
    QByteArray snapshot(editor->length() + 1, '\0');
    editor->send(SCI_GETTEXT, snapshot.size(), reinterpret_cast<sptr_t>(snapshot.data()));
    snapshot.resize(editor->length());
    const QString language = editor->languageName;
    const QString fileName = editor->getName();
    const quint64 version = generation;
    const bool needFunctions = functionDock->isVisible();
    const bool needMap = mapDock->isVisible();
    struct Analysis { QVector<NavigationSymbol> symbols; DocumentOverview overview; };
    const auto result = std::make_shared<Analysis>();
    running = true;
    functionStatus->setText(tr("Updating outline…"));
    auto *worker = QThread::create([snapshot = std::move(snapshot), language, fileName, needFunctions, needMap, result] {
        const auto cancelled = [] { return QThread::currentThread()->isInterruptionRequested(); };
        if (needFunctions) result->symbols = FunctionExtractor::extract(snapshot, language, fileName, cancelled);
        if (needMap) result->overview = FunctionExtractor::overview(snapshot, cancelled);
    });
    activeWorker = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, result, version, language, fileName, needFunctions, needMap] {
        running = false;
        activeWorker = nullptr;
        if (version == generation && editor) {
            if (needFunctions) {
                symbols = result->symbols;
                populateFunctions();
                if (FunctionExtractor::languageFor(language, fileName).isEmpty()) functionStatus->setText(tr("No outline parser for this language."));
            }
            if (needMap) {
                map->setOverview(result->overview);
                mapStatus->setText(tr("%1 lines · click to navigate").arg(result->overview.lineCount));
                updateVisibleRange();
            }
        }
        if (refreshPending) debounce.start(50);
    });
    worker->start();
}

void NavigationPanels::populateFunctions()
{
    functionList->clear();
    const QString query = filter->text();
    int shown = 0;
    for (const NavigationSymbol &symbol : symbols) {
        if (!symbol.name.contains(query, Qt::CaseInsensitive)) continue;
        auto *item = new QTreeWidgetItem(functionList, {symbol.name, QString::number(symbol.line + 1)});
        item->setData(0, Qt::UserRole, symbol.line);
        item->setToolTip(0, tr("%1 · line %2").arg(symbol.kind).arg(symbol.line + 1));
        ++shown;
    }
    functionStatus->setText(symbols.size() == FunctionExtractor::MaximumSymbols
        ? tr("%1 shown · outline capped at %2 symbols").arg(shown).arg(FunctionExtractor::MaximumSymbols)
        : tr("%1 of %2 symbols").arg(shown).arg(symbols.size()));
}

void NavigationPanels::updateVisibleRange()
{
    if (!editor || !mapDock->isVisible()) return;
    const auto first = editor->firstVisibleLine();
    const int startLine = int(editor->docLineFromVisible(first));
    const int endLine = int(editor->docLineFromVisible(first + std::max<sptr_t>(0, editor->linesOnScreen() - 1)));
    map->setVisibleRange(startLine, endLine < 0 ? int(editor->lineCount() - 1) : endLine);
}

void NavigationPanels::navigateTo(int line)
{
    if (!editor) return;
    line = std::clamp(line, 0, int(editor->lineCount() - 1));
    editor->ensureVisible(line);
    editor->gotoLine(line);
    editor->setFirstVisibleLine(std::max<sptr_t>(0, editor->visibleFromDocLine(line) - editor->linesOnScreen() / 2));
    editor->QWidget::setFocus(Qt::OtherFocusReason);
    updateVisibleRange();
}
