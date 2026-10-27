/* Copyright 2026 8bitPad contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NAVIGATIONPANELS_H
#define NAVIGATIONPANELS_H

#include "FunctionExtractor.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class MainWindow;
class NotepadNextApplication;
class ScintillaNext;
class QDockWidget;
class QLineEdit;
class QLabel;
class QTreeWidget;
class QThread;

// A document-density overview, deliberately not an editable shared Scintilla
// document. Keyboard input, IMEs and drops cannot mutate the source document.
class DocumentMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentMapWidget(QWidget *parent = nullptr);
    void setOverview(const DocumentOverview &value);
    void setVisibleRange(int first, int last);
    int lineAtY(qreal y) const;
signals:
    void navigateToLine(int line);
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    DocumentOverview overview;
    int firstVisible = 0;
    int lastVisible = 0;
};

class NavigationPanels : public QObject
{
    Q_OBJECT
public:
    NavigationPanels(MainWindow *window, NotepadNextApplication *app);
    ~NavigationPanels() override;
private:
    QPointer<MainWindow> window;
    QPointer<ScintillaNext> editor;
    QDockWidget *functionDock;
    QDockWidget *mapDock;
    QTreeWidget *functionList;
    QLineEdit *filter;
    QLabel *functionStatus;
    QLabel *mapStatus;
    DocumentMapWidget *map;
    QTimer debounce;
    quint64 generation = 0;
    bool running = false;
    bool refreshPending = false;
    QPointer<QThread> activeWorker;
    QVector<NavigationSymbol> symbols;

    void attachEditor(ScintillaNext *value);
    void scheduleRefresh();
    void refresh();
    void populateFunctions();
    void updateVisibleRange();
    void navigateTo(int line);
};

#endif
