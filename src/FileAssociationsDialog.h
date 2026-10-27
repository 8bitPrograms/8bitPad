// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "FileAssociations.h"

#include <QDialog>
#include <QMap>
#include <QSet>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class FileAssociationsDialog final : public QDialog
{
    Q_OBJECT
public:
    struct Callbacks {
        std::function<QList<FileAssociations::Group>()> catalog;
        std::function<QStringList()> registeredExtensions;
        std::function<FileAssociations::ParsedExtensions(const QString &)> parseExtensions;
        std::function<FileAssociations::BatchResult(const QStringList &, bool)> apply;
        std::function<bool()> contextMenuInstalled;
        std::function<FileAssociations::BatchResult(bool)> setContextMenuEnabled;
    };

    explicit FileAssociationsDialog(QWidget *parent = nullptr, Callbacks callbacks = {});
    QStringList selectedExtensions() const;

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void populateCatalog();
    QTreeWidgetItem *appendExtension(QTreeWidgetItem *group, const QString &extension);
    void filterExtensions();
    void updateSelectionSummary();
    void setSelection(bool checked, bool visibleOnly);
    void addCustomExtensions();
    void applySelection();
    void setContextMenu(bool enabled);
    void updateContextMenuState();
    void showStatus(const QString &message, const QStringList &errors = {});
    void refreshIcons();

    Callbacks callbacks;
    QSet<QString> registered;
    QMap<QString, QTreeWidgetItem *> extensionItems;
    QList<QTreeWidgetItem *> groups;
    QTreeWidgetItem *customGroup = nullptr;
    QLineEdit *search = nullptr;
    QTreeWidget *tree = nullptr;
    QPushButton *selectAll = nullptr;
    QPushButton *selectVisible = nullptr;
    QPushButton *clearAll = nullptr;
    QLineEdit *customInput = nullptr;
    QPushButton *addCustom = nullptr;
    QLabel *selectedCount = nullptr;
    QCheckBox *requestDefaults = nullptr;
    QLabel *contextState = nullptr;
    QPushButton *addContext = nullptr;
    QPushButton *removeContext = nullptr;
    QLabel *status = nullptr;
    QPushButton *apply = nullptr;
    bool changingItems = false;
    bool contextInstalled = false;
};
