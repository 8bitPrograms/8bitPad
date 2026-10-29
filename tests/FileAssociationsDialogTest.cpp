// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileAssociationsDialog.h"

#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QtTest>

namespace {
struct State {
    int applies = 0;
    int registeredReads = 0;
    int contextReads = 0;
    QStringList selected;
    bool requestDefaults = false;
    bool contextInstalled = false;
    QList<bool> contextWrites;
    QStringList registered {QStringLiteral("log")};
};

FileAssociationsDialog::Callbacks callbacksFor(State &state)
{
    FileAssociationsDialog::Callbacks callbacks;
    callbacks.catalog = [] {
        return QList<FileAssociations::Group>{
            {QStringLiteral("documents"), QStringLiteral("Documents"), {QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("md")}},
            {QStringLiteral("scripts"), QStringLiteral("Scripts"), {QStringLiteral("py"), QStringLiteral("rb")}},
            {QStringLiteral("data"), QStringLiteral("Data"), {QStringLiteral("json")}}
        };
    };
    callbacks.registeredExtensions = [&state] { ++state.registeredReads; return state.registered; };
    callbacks.parseExtensions = FileAssociations::parseExtensions;
    callbacks.apply = [&state](const QStringList &selected, bool defaults) {
        ++state.applies;
        state.selected = selected;
        state.requestDefaults = defaults;
        FileAssociations::BatchResult result;
        result.registeredExtensions = selected;
        if (defaults) result.defaultExtensions = selected;
        return result;
    };
    callbacks.contextMenuInstalled = [&state] { ++state.contextReads; return state.contextInstalled; };
    callbacks.setContextMenuEnabled = [&state](bool enabled) {
        state.contextWrites.append(enabled);
        state.contextInstalled = enabled;
        return FileAssociations::BatchResult{};
    };
    return callbacks;
}

QTreeWidgetItem *extensionItem(FileAssociationsDialog &dialog, const QString &extension)
{
    auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("associationTree"));
    for (int group = 0; group < tree->topLevelItemCount(); ++group) {
        auto *parent = tree->topLevelItem(group);
        for (int row = 0; row < parent->childCount(); ++row) {
            auto *item = parent->child(row);
            if (item->data(0, Qt::UserRole).toString() == extension)
                return item;
        }
    }
    return nullptr;
}

void click(FileAssociationsDialog &dialog, const char *name)
{
    auto *button = dialog.findChild<QPushButton *>(QLatin1String(name));
    QVERIFY2(button, name);
    button->click();
}
}

class FileAssociationsDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void openingAndSelectingDoNotWrite();
    void globalSelectAndClearIncludeFilteredTypes();
    void visibleSelectionAndGroupChecksRetainHiddenChoices();
    void customInputAddsMultipleAndDeduplicates();
    void invalidCustomInputIsAtomic();
    void applySendsOneExactBatch();
    void directAssociationResultsAreShownInline_data();
    void directAssociationResultsAreShownInline();
    void partialFailureKeepsSuccessfulRegistrationVisible();
    void contextMenuControlsAreIndependent();
    void themeRefreshPreservesSelection();
    void enterInInputsDoesNotApplyAssociations();
};

void FileAssociationsDialogTest::openingAndSelectingDoNotWrite()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    QCOMPARE(state.registeredReads, 1);
    QCOMPARE(state.contextReads, 1);
    QCOMPARE(state.applies, 0);
    QVERIFY(state.contextWrites.isEmpty());
    QVERIFY(dialog.selectedExtensions().isEmpty());
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("associationApply"))->isEnabled());
    QVERIFY(extensionItem(dialog, QStringLiteral("log"))->text(1).contains(QStringLiteral("Registered")));
    extensionItem(dialog, QStringLiteral("txt"))->setCheckState(0, Qt::Checked);
    extensionItem(dialog, QStringLiteral("py"))->setCheckState(0, Qt::Checked);
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("py"), QStringLiteral("txt")}));
    QCOMPARE(state.applies, 0);
    QVERIFY(state.contextWrites.isEmpty());
}

void FileAssociationsDialogTest::globalSelectAndClearIncludeFilteredTypes()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    dialog.findChild<QLineEdit *>(QStringLiteral("associationSearch"))->setText(QStringLiteral(".py"));
    QVERIFY(extensionItem(dialog, QStringLiteral("txt"))->isHidden());
    click(dialog, "associationSelectAll");
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("json"), QStringLiteral("log"), QStringLiteral("md"),
        QStringLiteral("py"), QStringLiteral("rb"), QStringLiteral("txt")}));
    click(dialog, "associationClearSelection");
    QVERIFY(dialog.selectedExtensions().isEmpty());
    QCOMPARE(state.applies, 0);
}

void FileAssociationsDialogTest::visibleSelectionAndGroupChecksRetainHiddenChoices()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    auto *ruby = extensionItem(dialog, QStringLiteral("rb"));
    auto *python = extensionItem(dialog, QStringLiteral("py"));
    ruby->setCheckState(0, Qt::Checked);
    dialog.findChild<QLineEdit *>(QStringLiteral("associationSearch"))->setText(QStringLiteral(".py"));
    click(dialog, "associationSelectVisible");
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("py"), QStringLiteral("rb")}));
    python->parent()->setCheckState(0, Qt::Unchecked);
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("rb")}));
    dialog.findChild<QLineEdit *>(QStringLiteral("associationSearch"))->clear();
    QCOMPARE(python->parent()->checkState(0), Qt::PartiallyChecked);
    python->parent()->setCheckState(0, Qt::Checked);
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("py"), QStringLiteral("rb")}));
    QCOMPARE(state.applies, 0);
}

void FileAssociationsDialogTest::customInputAddsMultipleAndDeduplicates()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    auto *input = dialog.findChild<QLineEdit *>(QStringLiteral("associationCustomInput"));
    input->setText(QStringLiteral(".FOO, *.bar; txt foo"));
    click(dialog, "associationAddCustom");
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("bar"), QStringLiteral("foo"), QStringLiteral("txt")}));
    QVERIFY(extensionItem(dialog, QStringLiteral("foo")));
    QVERIFY(input->text().isEmpty());
    input->setText(QStringLiteral("foo bar"));
    click(dialog, "associationAddCustom");
    QCOMPARE(dialog.selectedExtensions().size(), 3);
    QCOMPARE(state.applies, 0);
}

void FileAssociationsDialogTest::invalidCustomInputIsAtomic()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    auto *input = dialog.findChild<QLineEdit *>(QStringLiteral("associationCustomInput"));
    input->setText(QStringLiteral("good; bad/name"));
    click(dialog, "associationAddCustom");
    QVERIFY(!extensionItem(dialog, QStringLiteral("good")));
    QVERIFY(dialog.selectedExtensions().isEmpty());
    QVERIFY(!input->text().isEmpty());
    const auto *status = dialog.findChild<QLabel *>(QStringLiteral("associationStatus"));
    QVERIFY(status->text().contains(QStringLiteral("not added")));
    QCOMPARE(state.applies, 0);
}

void FileAssociationsDialogTest::applySendsOneExactBatch()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    extensionItem(dialog, QStringLiteral("txt"))->setCheckState(0, Qt::Checked);
    extensionItem(dialog, QStringLiteral("json"))->setCheckState(0, Qt::Checked);
    dialog.findChild<QCheckBox *>(QStringLiteral("associationRequestDefaults"))->setChecked(false);
    click(dialog, "associationApply");
    QCOMPARE(state.applies, 1);
    QCOMPARE(state.selected, QStringList({QStringLiteral("json"), QStringLiteral("txt")}));
    QVERIFY(!state.requestDefaults);
    QVERIFY(state.contextWrites.isEmpty());
    QVERIFY(extensionItem(dialog, QStringLiteral("json"))->text(1).contains(QStringLiteral("Registered")));
    dialog.findChild<QCheckBox *>(QStringLiteral("associationRequestDefaults"))->setChecked(true);
    click(dialog, "associationApply");
    QCOMPARE(state.applies, 2);
    QVERIFY(state.requestDefaults);
}

void FileAssociationsDialogTest::directAssociationResultsAreShownInline_data()
{
    QTest::addColumn<int>("pendingCount");
    QTest::newRow("single-pending-type") << 1;
    QTest::newRow("six-pending-types") << 6;
    QTest::newRow("long-pending-list") << 9;
}

void FileAssociationsDialogTest::directAssociationResultsAreShownInline()
{
    QFETCH(int, pendingCount);
    State state;
    QStringList pending;
    for (int i = 0; i < pendingCount; ++i)
        pending << QStringLiteral("custom%1").arg(i);
    auto callbacks = callbacksFor(state);
    callbacks.catalog = [&] {
        return QList<FileAssociations::Group>{{"text", "Text", QStringList{"txt"} + pending}};
    };
    callbacks.apply = [&](const QStringList &selected, bool defaults) {
        ++state.applies;
        state.requestDefaults = defaults;
        FileAssociations::BatchResult result;
        result.registeredExtensions = selected;
        result.defaultExtensions = {"txt"};
        result.pendingDefaultExtensions = pending;
        return result;
    };
    FileAssociationsDialog dialog(nullptr, callbacks);
    auto *defaults = dialog.findChild<QCheckBox *>(QStringLiteral("associationRequestDefaults"));
    QVERIFY(defaults->isChecked());
    QVERIFY(!defaults->text().contains(QStringLiteral("Open Windows")));
    extensionItem(dialog, QStringLiteral("txt"))->setCheckState(0, Qt::Checked);
    for (const QString &extension : pending)
        extensionItem(dialog, extension)->setCheckState(0, Qt::Checked);
    click(dialog, "associationApply");
    QCOMPARE(state.applies, 1);
    QVERIFY(state.requestDefaults);
    QCOMPARE(extensionItem(dialog, QStringLiteral("txt"))->text(1), QStringLiteral("Default"));
    for (const QString &extension : pending)
        QCOMPARE(extensionItem(dialog, extension)->text(1), QStringLiteral("Needs Windows confirmation"));
#ifdef Q_OS_WIN
    const QString message = dialog.findChild<QLabel *>(QStringLiteral("associationStatus"))->text();
    QVERIFY(message.contains(QStringLiteral("Default application set for 1 file types.")));
    QVERIFY(message.contains(QStringLiteral(".custom0")));
    if (pendingCount > 6) {
        QVERIFY(message.contains(QStringLiteral("3 more")));
        QVERIFY(!message.contains(QStringLiteral(".custom6")));
    }
#endif
}

void FileAssociationsDialogTest::partialFailureKeepsSuccessfulRegistrationVisible()
{
    State state;
    auto callbacks = callbacksFor(state);
    callbacks.apply = [&state](const QStringList &selected, bool) {
        ++state.applies;
        state.selected = selected;
        FileAssociations::BatchResult result;
        result.registeredExtensions = {QStringLiteral("txt")};
        result.errors = {QStringLiteral("Could not register .json")};
        return result;
    };
    FileAssociationsDialog dialog(nullptr, callbacks);
    extensionItem(dialog, QStringLiteral("txt"))->setCheckState(0, Qt::Checked);
    extensionItem(dialog, QStringLiteral("json"))->setCheckState(0, Qt::Checked);
    click(dialog, "associationApply");
    QVERIFY(extensionItem(dialog, QStringLiteral("txt"))->text(1).contains(QStringLiteral("Registered")));
    QVERIFY(extensionItem(dialog, QStringLiteral("json"))->text(1).isEmpty());
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("associationStatus"))->text().contains(QStringLiteral("Could not register .json")));
    QCOMPARE(dialog.selectedExtensions().size(), 2);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("associationApply"))->isEnabled());
}

void FileAssociationsDialogTest::contextMenuControlsAreIndependent()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    extensionItem(dialog, QStringLiteral("txt"))->setCheckState(0, Qt::Checked);
    click(dialog, "associationAddContextMenu");
    QVERIFY(state.contextInstalled);
    QCOMPARE(state.contextWrites, QList<bool>({true}));
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("associationRemoveContextMenu"))->isEnabled());
    click(dialog, "associationRemoveContextMenu");
    QVERIFY(!state.contextInstalled);
    QCOMPARE(state.contextWrites, QList<bool>({true, false}));
    QCOMPARE(state.applies, 0);
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("txt")}));
    QVERIFY(extensionItem(dialog, QStringLiteral("log"))->text(1).contains(QStringLiteral("Registered")));
}

void FileAssociationsDialogTest::themeRefreshPreservesSelection()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    extensionItem(dialog, QStringLiteral("txt"))->setCheckState(0, Qt::Checked);
    extensionItem(dialog, QStringLiteral("py"))->setCheckState(0, Qt::Checked);
    const QStringList before = dialog.selectedExtensions();
    QEvent palette(QEvent::PaletteChange);
    QCoreApplication::sendEvent(&dialog, &palette);
    QEvent style(QEvent::StyleChange);
    QCoreApplication::sendEvent(&dialog, &style);
    QCOMPARE(dialog.selectedExtensions(), before);
    QCOMPARE(state.applies, 0);
}

void FileAssociationsDialogTest::enterInInputsDoesNotApplyAssociations()
{
    State state;
    FileAssociationsDialog dialog(nullptr, callbacksFor(state));
    dialog.show();
    auto *input = dialog.findChild<QLineEdit *>(QStringLiteral("associationCustomInput"));
    input->setText(QStringLiteral("foo bar"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(dialog.selectedExtensions(), QStringList({QStringLiteral("bar"), QStringLiteral("foo")}));
    QCOMPARE(state.applies, 0);
    auto *filter = dialog.findChild<QLineEdit *>(QStringLiteral("associationSearch"));
    filter->setText(QStringLiteral(".foo"));
    QTest::keyClick(filter, Qt::Key_Return);
    QCOMPARE(state.applies, 0);
    QVERIFY(dialog.isVisible());
}

QTEST_MAIN(FileAssociationsDialogTest)
#include "FileAssociationsDialogTest.moc"
