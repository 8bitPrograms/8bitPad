#pragma once

#include "FolderSearch.h"
#include <QDialog>
#include <QFutureWatcher>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QLabel;

class FolderSearchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FolderSearchDialog(QWidget *parent = nullptr);
    ~FolderSearchDialog() override;
    void setDirectory(const QString &path);
    void setOpenFiles(const QStringList &paths);

public slots:
    void reject() override;

signals:
    void openResult(const QString &path, int line, int column);
    void aboutToReplace();

private:
    void startSearch();
    void startReplace();
    void setBusy(bool busy);
    void invalidatePreview();
    void displayResults();

    QLineEdit *directory;
    QLineEdit *pattern;
    QLineEdit *replacement;
    QLineEdit *masks;
    QCheckBox *recursive;
    QCheckBox *regex;
    QCheckBox *matchCase;
    QCheckBox *wholeWord;
    QPushButton *searchButton;
    QPushButton *replaceButton;
    QPushButton *cancelButton;
    QPushButton *browseButton;
    QTableWidget *table;
    QLabel *status;
    QStringList openFiles;
    FolderSearch::Result preview;
    FolderSearch::Cancellation cancellation;
    QFutureWatcher<FolderSearch::Result> searchWatcher;
    QFutureWatcher<FolderSearch::ReplaceResult> replaceWatcher;
    bool busy = false;
    bool validPreview = false;
};
