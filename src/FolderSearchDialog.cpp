#include "FolderSearchDialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

FolderSearchDialog::FolderSearchDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Find and Replace in Files"));
    setWindowModality(Qt::WindowModal);
    resize(1000, 650);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    auto *folderRow = new QHBoxLayout;
    directory = new QLineEdit(QDir::homePath(), this);
    browseButton = new QPushButton(tr("Browse…"), this);
    folderRow->addWidget(directory);
    folderRow->addWidget(browseButton);
    form->addRow(tr("Folder"), folderRow);
    pattern = new QLineEdit(this);
    pattern->setPlaceholderText(tr("Search text or regular expression"));
    replacement = new QLineEdit(this);
    replacement->setPlaceholderText(tr("Replacement; regex supports \\1, \\n, \\r, \\t"));
    masks = new QLineEdit(QStringLiteral("*"), this);
    masks->setPlaceholderText(tr("*.cpp;*.h;*.txt"));
    form->addRow(tr("Find"), pattern);
    form->addRow(tr("Replace with"), replacement);
    form->addRow(tr("File masks"), masks);
    layout->addLayout(form);

    auto *options = new QHBoxLayout;
    recursive = new QCheckBox(tr("Subfolders"), this);
    recursive->setChecked(true);
    regex = new QCheckBox(tr("Regular expression"), this);
    matchCase = new QCheckBox(tr("Match case"), this);
    wholeWord = new QCheckBox(tr("Whole word"), this);
    for (auto *option : {recursive, regex, matchCase, wholeWord}) options->addWidget(option);
    options->addStretch();
    layout->addLayout(options);

    auto *buttons = new QHBoxLayout;
    searchButton = new QPushButton(tr("Search / Preview"), this);
    replaceButton = new QPushButton(tr("Replace previewed matches…"), this);
    cancelButton = new QPushButton(tr("Cancel operation"), this);
    replaceButton->setEnabled(false);
    cancelButton->setEnabled(false);
    buttons->addWidget(searchButton);
    buttons->addWidget(replaceButton);
    buttons->addStretch();
    buttons->addWidget(cancelButton);
    layout->addLayout(buttons);

    table = new QTableWidget(0, 4, this);
    table->setHorizontalHeaderLabels({tr("File"), tr("Line"), tr("Column"), tr("Text")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 300);
    table->setColumnWidth(1, 65);
    table->setColumnWidth(2, 65);
    layout->addWidget(table, 1);
    status = new QLabel(tr("Preview first. Files open in the editor are skipped during replacement."), this);
    status->setWordWrap(true);
    layout->addWidget(status);

    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(this, tr("Search folder"), directory->text());
        if (!chosen.isEmpty()) directory->setText(chosen);
    });
    connect(searchButton, &QPushButton::clicked, this, &FolderSearchDialog::startSearch);
    connect(pattern, &QLineEdit::returnPressed, this, &FolderSearchDialog::startSearch);
    connect(replaceButton, &QPushButton::clicked, this, &FolderSearchDialog::startReplace);
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        if (cancellation) cancellation->store(true);
        status->setText(tr("Cancelling after the current file operation…"));
    });
    for (auto *field : {directory, pattern, masks})
        connect(field, &QLineEdit::textChanged, this, &FolderSearchDialog::invalidatePreview);
    for (auto *option : {recursive, regex, matchCase, wholeWord})
        connect(option, &QCheckBox::toggled, this, &FolderSearchDialog::invalidatePreview);
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!busy && table->item(row, 0)) {
            hide();
            emit openResult(table->item(row, 0)->data(Qt::UserRole).toString(), table->item(row, 1)->text().toInt(), table->item(row, 2)->text().toInt());
        }
    });
    connect(&searchWatcher, &QFutureWatcher<FolderSearch::Result>::finished, this, [this] {
        preview = searchWatcher.result();
        validPreview = preview.error.isEmpty() && !preview.cancelled && !preview.truncated;
        setBusy(false);
        displayResults();
    });
    connect(&replaceWatcher, &QFutureWatcher<FolderSearch::ReplaceResult>::finished, this, [this] {
        const auto result = replaceWatcher.result();
        validPreview = false;
        setBusy(false);
        status->setText(tr("Replaced %1 matches in %2 files; skipped %3. %4 backups retained.%5")
                            .arg(result.matches).arg(result.files).arg(result.skipped).arg(result.backups.size())
                            .arg(result.cancelled ? tr(" Cancelled; completed replacements remain saved.") : QString()));
        if (!result.errors.isEmpty()) {
            QMessageBox report(QMessageBox::Warning, tr("Replacement report"), tr("Some files were skipped. See details."), QMessageBox::Ok, this);
            report.setDetailedText(result.errors.join(QLatin1Char('\n')));
            report.exec();
        }
    });
}

FolderSearchDialog::~FolderSearchDialog()
{
    if (cancellation) cancellation->store(true);
}

void FolderSearchDialog::reject()
{
    if (cancellation) cancellation->store(true);
    // Keep the main window blocked until the worker stops writing. Otherwise a
    // newly opened document could appear after the worker's open-file snapshot.
    if (busy && replaceWatcher.isRunning()) {
        status->setText(tr("Cancelling after the current file operation. Close this window when it finishes."));
        return;
    }
    QDialog::reject();
}

void FolderSearchDialog::setDirectory(const QString &path) { if (!busy) directory->setText(path); }
void FolderSearchDialog::setOpenFiles(const QStringList &paths) { openFiles = paths; }

void FolderSearchDialog::invalidatePreview()
{
    validPreview = false;
    replaceButton->setEnabled(false);
}

void FolderSearchDialog::setBusy(bool value)
{
    busy = value;
    for (auto *field : {directory, pattern, replacement, masks}) field->setEnabled(!busy);
    for (auto *option : {recursive, regex, matchCase, wholeWord}) option->setEnabled(!busy);
    browseButton->setEnabled(!busy);
    table->setEnabled(!busy);
    searchButton->setEnabled(!busy);
    replaceButton->setEnabled(!busy && validPreview && preview.matches > 0);
    cancelButton->setEnabled(busy);
}

void FolderSearchDialog::startSearch()
{
    if (busy) return;
    FolderSearch::Options options;
    options.directory = directory->text();
    options.pattern = pattern->text();
    options.masks = masks->text().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (auto &mask : options.masks) mask = mask.trimmed();
    if (options.masks.isEmpty()) options.masks = {QStringLiteral("*")};
    options.recursive = recursive->isChecked();
    options.regex = regex->isChecked();
    options.caseSensitive = matchCase->isChecked();
    options.wholeWord = wholeWord->isChecked();
    cancellation = std::make_shared<std::atomic_bool>(false);
    invalidatePreview();
    setBusy(true);
    status->setText(tr("Searching… Limits: 10,000 files, 5,000 matches, 32 MiB per file."));
    searchWatcher.setFuture(QtConcurrent::run(FolderSearch::scan, options, cancellation));
}

void FolderSearchDialog::displayResults()
{
    table->setRowCount(0);
    table->setUpdatesEnabled(false);
    for (const auto &file : preview.files) {
        for (const auto &match : file.matches) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto *path = new QTableWidgetItem(QDir(preview.options.directory).relativeFilePath(file.path));
            path->setData(Qt::UserRole, file.path);
            path->setToolTip(file.path);
            table->setItem(row, 0, path);
            table->setItem(row, 1, new QTableWidgetItem(QString::number(match.line)));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(match.column)));
            table->setItem(row, 3, new QTableWidgetItem(match.preview));
        }
    }
    table->setUpdatesEnabled(true);
    if (!preview.error.isEmpty()) { status->setText(preview.error); return; }
    status->setText(tr("%1 matches in %2 files; %3 scanned; %4 binary, unsupported, oversized or unreadable files skipped.%5%6")
                        .arg(preview.matches).arg(preview.files.size()).arg(preview.scanned).arg(preview.skipped)
                        .arg(preview.cancelled ? tr(" Cancelled; replacement disabled.") : QString())
                        .arg(preview.truncated ? tr(" Limit reached; narrow the search before replacing.") : QString()));
}

void FolderSearchDialog::startReplace()
{
    if (busy || !validPreview || preview.matches == 0) return;
    const QString explanation = tr("Replace %1 previewed matches across up to %2 files?\n\nFind: %3\nReplace with: %4\n\nOpen documents and files changed since the preview are skipped. A unique .8bitpad.bak-… copy is kept beside every changed file.")
                                    .arg(preview.matches).arg(preview.files.size()).arg(preview.options.pattern, replacement->text());
    if (QMessageBox::question(this, tr("Replace in files"), explanation, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    emit aboutToReplace();
    cancellation = std::make_shared<std::atomic_bool>(false);
    setBusy(true);
    status->setText(tr("Replacing previewed matches and saving backups…"));
    replaceWatcher.setFuture(QtConcurrent::run(FolderSearch::replace, preview, replacement->text(), openFiles, cancellation));
}
